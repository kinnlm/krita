# SPDX-License-Identifier: GPL-3.0-or-later
"""Painterly PBR Light exporter implementation.

This module exposes a PyKrita extension that can traverse the new material
layer groups and export their contents as PBR texture sets that are ready for
Godot, Unreal, Blender or physical printing pipelines.
"""

from __future__ import annotations

import os
import pathlib
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

try:  # pragma: no cover - allow running tests without Krita runtime
    from krita import Document, Extension, InfoObject, Krita, Node, i18n
except ImportError:  # pragma: no cover - fallback stubs for headless testing
    Document = object  # type: ignore[misc, assignment]

    class Extension:  # type: ignore[override]
        def __init__(self, parent) -> None:
            self._parent = parent

    class InfoObject:  # type: ignore[override]
        pass

    class _KritaFallback:
        @staticmethod
        def instance():
            raise RuntimeError("Krita runtime is not available")

    Krita = _KritaFallback  # type: ignore[assignment]

    class Node:  # type: ignore[override]
        pass

    def i18n(text: str) -> str:  # type: ignore[override]
        return text

try:  # Krita currently ships PyQt5, but support PyQt6 just in case.
    from PyQt6.QtCore import Qt
    from PyQt6.QtWidgets import (
        QCheckBox,
        QComboBox,
        QDialog,
        QDialogButtonBox,
        QFileDialog,
        QFormLayout,
        QHBoxLayout,
        QLineEdit,
        QPushButton,
        QWidget,
    )
except ImportError:  # pragma: no cover - runtime guard.
    from PyQt5.QtCore import Qt  # type: ignore
    from PyQt5.QtWidgets import (  # type: ignore
        QCheckBox,
        QComboBox,
        QDialog,
        QDialogButtonBox,
        QFileDialog,
        QFormLayout,
        QHBoxLayout,
        QLineEdit,
        QPushButton,
        QWidget,
    )

try:
    from PyQt6.QtGui import QImage, QColor
except ImportError:  # pragma: no cover - runtime guard.
    from PyQt5.QtGui import QImage, QColor  # type: ignore

FORMAT_RGBA8888 = getattr(QImage.Format, "Format_RGBA8888", getattr(QImage, "Format_RGBA8888"))
FORMAT_GRAYSCALE16 = getattr(QImage.Format, "Format_Grayscale16", getattr(QImage, "Format_Grayscale16"))
FORMAT_GRAYSCALE8 = getattr(QImage.Format, "Format_Grayscale8", getattr(QImage, "Format_Grayscale8"))


HEIGHT_FORMATS: Tuple[Tuple[str, Tuple[str, str]], ...] = (
    ("16-bit PNG (.png)", (".png", "U16")),
    ("32-bit EXR (.exr)", (".exr", "F16")),
)

ROUGHNESS_DEPTHS: Tuple[Tuple[str, Tuple[str, str]], ...] = (
    ("8-bit PNG", (".png", "U8")),
    ("16-bit PNG", (".png", "U16")),
)


@dataclass
class ExportOptions:
    document: Document
    output_dir: str
    prefix: str
    flip_green: bool
    bake_normals: bool
    pack_orm: bool
    height_extension: str
    height_depth: str
    roughness_extension: str
    roughness_depth: str


@dataclass
class ManifestEntry:
    name: str
    files: Dict[str, str]
    baked_normals: bool
    packed_orm: bool


class PainterlyPbrExporterExtension(Extension):
    """Registers a menu action that exports material groups."""

    def __init__(self, parent: Krita):
        super().__init__(parent)
        self._application = parent

    def setup(self) -> None:  # noqa: D401 - PyKrita interface
        """Called on Krita start-up."""

    def createActions(self, window) -> None:  # noqa: N802 - PyKrita naming
        action = window.createAction(
            "painterly_pbr_export",
            i18n("Export PBR Set…"),
            "file/export",
        )
        action.triggered.connect(lambda: self._export_from_window(window))

    # ------------------------------------------------------------------
    # Export implementation
    # ------------------------------------------------------------------

    def _export_from_window(self, window) -> None:
        document = window.activeDocument()
        if document is None:
            window.showMessage(i18n("No document available for export."), 5000)
            return

        parent_widget = window.qwindow() if hasattr(window, "qwindow") else None
        dialog = _ExportDialog(parent_widget, self._default_output_dir(document), self._default_prefix(document))
        if dialog.exec() != QDialog.DialogCode.Accepted:
            return

        options = dialog.build_options(document)
        os.makedirs(options.output_dir, exist_ok=True)

        manifest_entries = self._export_document(options)
        if manifest_entries:
            self._write_manifest(options, manifest_entries)
            message = i18n("Exported %1 material set(s) to %2.")
            message = message.replace("%1", str(len(manifest_entries)))
            message = message.replace("%2", options.output_dir)
            window.showMessage(message, 6000)
        else:
            window.showMessage(i18n("No material groups ready for export."), 6000)

    def _default_output_dir(self, document: Document) -> str:
        base_dir = os.path.dirname(document.fileName()) or os.getcwd()
        return os.path.join(base_dir, "PainterlyPBR")

    def _default_prefix(self, document: Document) -> str:
        name = document.fileName()
        if name:
            return pathlib.Path(name).stem + "_"
        return document.name() + "_"

    def _export_document(self, options: ExportOptions) -> List[ManifestEntry]:
        root = options.document.rootNode()
        groups = self._find_material_groups(root)
        if not groups:
            return []

        manifest_entries: List[ManifestEntry] = []
        for group in groups:
            entry = self._export_material_group(group, options)
            if entry:
                manifest_entries.append(entry)
        return manifest_entries

    def _find_material_groups(self, root: Node) -> List[Node]:
        result: List[Node] = []
        stack: List[Node] = [root]
        while stack:
            current = stack.pop()
            if current.type() == "grouplayer" and bool(current.property("materialGroup")):
                result.append(current)
            for child in current.childNodes():
                stack.append(child)
        return result

    def _export_material_group(self, group: Node, options: ExportOptions) -> Optional[ManifestEntry]:
        channel_nodes = self._collect_channel_nodes(group)
        if not channel_nodes:
            return None

        group_slug = self._slugify(group.name())
        base_name = f"{options.prefix}{group_slug}" if options.prefix else group_slug
        files: Dict[str, str] = {}
        baked_normals = False

        # Base Color
        base_layer = channel_nodes.get("BaseColor")
        base_path = None
        if base_layer and base_layer.visible():
            base_path = self._export_channel_layer(
                options,
                group,
                base_layer,
                channel_name="BaseColor",
                extension=".png",
                forced_depth="U8",
            )
            if base_path:
                files["BaseColor"] = os.path.basename(base_path)

        # Height
        height_layer = channel_nodes.get("Height")
        height_path = None
        if height_layer and height_layer.visible():
            height_path = self._export_channel_layer(
                options,
                group,
                height_layer,
                channel_name="Height",
                extension=options.height_extension,
                forced_depth=options.height_depth,
            )
            if height_path:
                files["Height"] = os.path.basename(height_path)

        # Normal
        normal_layer = channel_nodes.get("Normal")
        normal_path = None
        if normal_layer and normal_layer.visible():
            normal_path = self._export_channel_layer(
                options,
                group,
                normal_layer,
                channel_name="Normal",
                extension=".png",
                forced_depth="U8",
            )
            if normal_path and options.flip_green:
                self._flip_normal_green(normal_path)
        elif options.bake_normals and height_layer:
            normal_path = self._bake_normals_from_height(options, group, height_layer, base_name, options.flip_green)
            baked_normals = bool(normal_path)
        if normal_path:
            files["Normal"] = os.path.basename(normal_path)

        # Roughness
        roughness_layer = channel_nodes.get("Roughness")
        roughness_path = None
        if roughness_layer and roughness_layer.visible():
            roughness_path = self._export_channel_layer(
                options,
                group,
                roughness_layer,
                channel_name="Roughness",
                extension=options.roughness_extension,
                forced_depth=options.roughness_depth,
            )
            if roughness_path:
                files["Roughness"] = os.path.basename(roughness_path)

        # Metallic
        metallic_layer = channel_nodes.get("Metallic")
        metallic_path = None
        if metallic_layer and metallic_layer.visible():
            metallic_path = self._export_channel_layer(
                options,
                group,
                metallic_layer,
                channel_name="Metallic",
                extension=options.roughness_extension,
                forced_depth=options.roughness_depth,
            )
            if metallic_path:
                files["Metallic"] = os.path.basename(metallic_path)

        if options.pack_orm and roughness_path and metallic_path:
            occlusion_layer = channel_nodes.get("Occlusion")
            occlusion_path = None
            if occlusion_layer and occlusion_layer.visible():
                occlusion_path = self._export_channel_layer(
                    options,
                    group,
                    occlusion_layer,
                    channel_name="Occlusion",
                    extension=options.roughness_extension,
                    forced_depth=options.roughness_depth,
                )
                if occlusion_path:
                    files["Occlusion"] = os.path.basename(occlusion_path)
            orm_filename = f"{base_name}_ORM.png"
            orm_path = os.path.join(options.output_dir, orm_filename)
            if self._pack_orm_texture(orm_path, roughness_path, metallic_path, occlusion_path):
                files["ORM"] = orm_filename

        if not files:
            return None

        return ManifestEntry(name=group_slug, files=files, baked_normals=baked_normals, packed_orm="ORM" in files)

    def _collect_channel_nodes(self, group: Node) -> Dict[str, Node]:
        mapping: Dict[str, Node] = {}
        for child in group.childNodes():
            channel_id = child.property("materialChannel")
            if channel_id:
                mapping[str(channel_id)] = child
        return mapping

    def _export_channel_layer(
        self,
        options: ExportOptions,
        group: Node,
        layer: Node,
        *,
        channel_name: str,
        extension: str,
        forced_depth: Optional[str],
        override_path: Optional[str] = None,
    ) -> Optional[str]:
        document = options.document
        app = Krita.instance()
        width = document.width()
        height = document.height()

        temp_doc = app.createDocument(
            width,
            height,
            f"__painterly_export_{channel_name}",
            layer.colorModel(),
            layer.colorDepth(),
            layer.colorProfile(),
            document.resolution(),
        )
        temp_doc.setBatchmode(True)
        duplicate = layer.duplicate()
        temp_doc.rootNode().addChildNode(duplicate, None)
        temp_doc.refreshProjection()
        temp_doc.waitForDone()

        target_profile = layer.colorProfile() or document.colorProfile()
        target_model = duplicate.colorModel()
        target_depth = forced_depth or duplicate.colorDepth()
        try:
            temp_doc.setColorSpace(target_model, target_depth, target_profile)
            temp_doc.waitForDone()
        except Exception:
            # As a fallback, try to convert only the layer.
            try:
                duplicate.setColorSpace(target_model, target_depth, target_profile)
                temp_doc.waitForDone()
            except Exception:
                pass

        if override_path:
            path = override_path
        else:
            filename = f"{self._slugify(group.name())}_{channel_name}{extension}"
            if options.prefix:
                filename = f"{options.prefix}{filename}"
            path = os.path.join(options.output_dir, filename)

        info = InfoObject()
        success = temp_doc.exportImage(path, info)
        temp_doc.waitForDone()
        temp_doc.close()
        if not success:
            return None
        return path

    def _flip_normal_green(self, path: str) -> None:
        image = QImage(path)
        if image.isNull():
            return
        image = image.convertToFormat(FORMAT_RGBA8888)
        ptr = image.bits()
        ptr.setsize(image.byteCount())
        data = bytearray(ptr)
        for i in range(1, len(data), 4):
            data[i] = 255 - data[i]
        result = QImage(image.width(), image.height(), FORMAT_RGBA8888)
        result.fill(QColor(0, 0, 0, 255))
        dest_ptr = result.bits()
        dest_ptr.setsize(result.byteCount())
        dest_ptr[:] = data
        result.save(path)

    def _bake_normals_from_height(
        self,
        options: ExportOptions,
        group: Node,
        height_layer: Node,
        base_name: str,
        flip_green: bool,
    ) -> Optional[str]:
        import tempfile

        tmp_file = tempfile.NamedTemporaryFile(suffix=".png", delete=False)
        tmp_file.close()
        try:
            temp_height_path = self._export_channel_layer(
                options,
                group,
                height_layer,
                channel_name="Height",
                extension=".png",
                forced_depth="U16",
                override_path=tmp_file.name,
            )
            if not temp_height_path:
                return None

            image = QImage(temp_height_path)
            if image.isNull():
                return None

            width = image.width()
            height = image.height()
            gray = image.convertToFormat(FORMAT_GRAYSCALE16)
            ptr = gray.bits()
            ptr.setsize(gray.byteCount())
            try:
                import numpy as np  # type: ignore
            except ImportError:
                return None

            data = np.frombuffer(ptr, dtype=np.uint16).astype(np.float32)
            data = data.reshape((height, width)) / 65535.0
            gy, gx = np.gradient(data)
            strength = 2.0
            nx = -gx * strength
            ny = -gy * strength
            nz = np.ones_like(nx)
            length = np.sqrt(nx * nx + ny * ny + nz * nz)
            nx /= length
            ny /= length
            nz /= length
            if flip_green:
                ny = -ny
            normals = np.stack([(nx + 1.0) * 0.5, (ny + 1.0) * 0.5, (nz + 1.0) * 0.5], axis=-1)
            alpha = np.ones((height, width, 1), dtype=np.float32)
            normals = np.concatenate([normals, alpha], axis=-1)
            normals = np.clip(normals * 255.0, 0, 255).astype(np.uint8)
            data_bytes = normals.tobytes()
            qimage = QImage(data_bytes, width, height, FORMAT_RGBA8888)
            qimage = qimage.copy()
            output_path = os.path.join(options.output_dir, f"{base_name}_Normal.png")
            qimage.save(output_path)
            return output_path
        finally:
            try:
                os.remove(tmp_file.name)
            except OSError:
                pass

    def _pack_orm_texture(
        self,
        orm_path: str,
        roughness_path: str,
        metallic_path: str,
        occlusion_path: Optional[str],
    ) -> bool:
        roughness = QImage(roughness_path)
        metallic = QImage(metallic_path)
        if roughness.isNull() or metallic.isNull():
            return False
        if roughness.size() != metallic.size():
            metallic = metallic.scaled(roughness.width(), roughness.height())

        occlusion = QImage(occlusion_path) if occlusion_path else QImage()
        if occlusion.isNull():
            occlusion = QImage(roughness.width(), roughness.height(), FORMAT_GRAYSCALE8)
            occlusion.fill(QColor(255, 255, 255))

        r_bytes = self._grayscale_bytes(occlusion, roughness.width(), roughness.height())
        g_bytes = self._grayscale_bytes(roughness, roughness.width(), roughness.height())
        b_bytes = self._grayscale_bytes(metallic, roughness.width(), roughness.height())
        a_bytes = bytes([255]) * (roughness.width() * roughness.height())

        data = bytearray(roughness.width() * roughness.height() * 4)
        data[0::4] = r_bytes
        data[1::4] = g_bytes
        data[2::4] = b_bytes
        data[3::4] = a_bytes
        qimage = QImage(bytes(data), roughness.width(), roughness.height(), FORMAT_RGBA8888)
        qimage = qimage.copy()
        return qimage.save(orm_path)

    def _grayscale_bytes(self, image: QImage, width: int, height: int) -> bytes:
        if image.width() != width or image.height() != height:
            image = image.scaled(width, height, Qt.AspectRatioMode.IgnoreAspectRatio, Qt.TransformationMode.SmoothTransformation)
        gray = image.convertToFormat(FORMAT_GRAYSCALE8)
        ptr = gray.bits()
        ptr.setsize(gray.byteCount())
        return bytes(ptr)

    def _write_manifest(self, options: ExportOptions, entries: List[ManifestEntry]) -> None:
        manifest_lines = ["materials:"]
        for entry in entries:
            manifest_lines.append(f"  - name: {entry.name}")
            manifest_lines.append(f"    bakedNormals: {'true' if entry.baked_normals else 'false'}")
            manifest_lines.append(f"    packedORM: {'true' if entry.packed_orm else 'false'}")
            for channel, filename in entry.files.items():
                manifest_lines.append(f"    {channel}: {filename}")
        manifest_lines.append("options:")
        manifest_lines.append(f"  flipGreen: {'true' if options.flip_green else 'false'}")
        manifest_lines.append(f"  bakeNormalsOnExport: {'true' if options.bake_normals else 'false'}")
        manifest_lines.append(f"  packORM: {'true' if options.pack_orm else 'false'}")
        manifest_lines.append(f"  heightDepth: {options.height_depth}")
        manifest_lines.append(f"  roughnessDepth: {options.roughness_depth}")

        manifest_path = os.path.join(options.output_dir, "material.yaml")
        with open(manifest_path, "w", encoding="utf-8") as handle:
            handle.write("\n".join(manifest_lines) + "\n")

    def _slugify(self, name: str) -> str:
        safe = [c if c.isalnum() or c in ("-", "_") else "_" for c in name]
        return "".join(safe).strip("_") or "Material"


class _ExportDialog(QDialog):
    """Collects export options from the user."""

    def __init__(self, parent: Optional[QWidget], default_dir: str, default_prefix: str) -> None:
        super().__init__(parent)
        self.setWindowTitle(i18n("Export Painterly PBR Set"))
        self._directory_edit = QLineEdit(default_dir)
        self._prefix_edit = QLineEdit(default_prefix)
        self._flip_green = QCheckBox(i18n("Flip normal green channel (DirectX Y-)",))
        self._bake_normals = QCheckBox(i18n("Bake normals from Height if missing"))
        self._pack_orm = QCheckBox(i18n("Pack ORM texture (R=Occlusion, G=Roughness, B=Metallic)"))
        self._height_combo = QComboBox()
        for label, payload in HEIGHT_FORMATS:
            self._height_combo.addItem(label, payload)
        self._roughness_combo = QComboBox()
        for label, payload in ROUGHNESS_DEPTHS:
            self._roughness_combo.addItem(label, payload)
        self._setup_layout()

    def _setup_layout(self) -> None:
        layout = QFormLayout(self)

        browse_button = QPushButton(i18n("Browse…"))
        browse_button.clicked.connect(self._browse_output)
        directory_row = QHBoxLayout()
        directory_row.addWidget(self._directory_edit)
        directory_row.addWidget(browse_button)
        layout.addRow(i18n("Output folder"), directory_row)

        layout.addRow(i18n("File prefix"), self._prefix_edit)
        layout.addRow(i18n("Height bit depth"), self._height_combo)
        layout.addRow(i18n("Roughness/Metallic bit depth"), self._roughness_combo)
        layout.addRow(self._flip_green)
        layout.addRow(self._bake_normals)
        layout.addRow(self._pack_orm)

        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addRow(buttons)

    def _browse_output(self) -> None:
        directory = QFileDialog.getExistingDirectory(self, i18n("Select export folder"), self._directory_edit.text())
        if directory:
            self._directory_edit.setText(directory)

    def build_options(self, document: Document) -> ExportOptions:
        prefix = self._prefix_edit.text().strip()
        if prefix and not prefix.endswith("_"):
            prefix += "_"
        height_extension, height_depth = self._height_combo.currentData()
        roughness_extension, roughness_depth = self._roughness_combo.currentData()
        return ExportOptions(
            document=document,
            output_dir=self._directory_edit.text().strip(),
            prefix=prefix,
            flip_green=self._flip_green.isChecked(),
            bake_normals=self._bake_normals.isChecked(),
            pack_orm=self._pack_orm.isChecked(),
            height_extension=height_extension,
            height_depth=height_depth,
            roughness_extension=roughness_extension,
            roughness_depth=roughness_depth,
        )
