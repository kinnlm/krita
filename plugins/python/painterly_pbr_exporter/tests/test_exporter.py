import builtins
import os
import sys
import tempfile
import types
import unittest
import zipfile
from typing import Dict, List, Optional

import numpy as np
from PIL import Image, ImageCms
from PyQt5.QtGui import QColor, QImage

_krita_stub = types.ModuleType("krita")


class _DummyExtension:
    def __init__(self, parent) -> None:  # pragma: no cover - compatibility shim
        self._parent = parent


_krita_stub.Document = object
_krita_stub.Extension = _DummyExtension
_krita_stub.InfoObject = object
_krita_stub.Krita = type("KritaPlaceholder", (), {"instance": staticmethod(lambda: None)})
_krita_stub.Node = object
_krita_stub.i18n = lambda text: text

sys.modules["krita"] = _krita_stub

from plugins.python.painterly_pbr_exporter import exporter

FORMAT_RGBA8888 = getattr(QImage.Format, "Format_RGBA8888", getattr(QImage, "Format_RGBA8888"))
FORMAT_GRAYSCALE16 = getattr(QImage.Format, "Format_Grayscale16", getattr(QImage, "Format_Grayscale16"))
FORMAT_GRAYSCALE8 = getattr(QImage.Format, "Format_Grayscale8", getattr(QImage, "Format_Grayscale8"))


class _FakeInfoObject:  # pragma: no cover - minimal stub
    pass


class _FakeNode:
    def __init__(
        self,
        name: str,
        *,
        node_type: str = "paintlayer",
        image: Optional[QImage] = None,
        properties: Optional[Dict[str, object]] = None,
        visible: bool = True,
        color_model: str = "RGBA",
        color_depth: str = "U8",
        color_profile: Optional[str] = None,
        children: Optional[List["_FakeNode"]] = None,
    ) -> None:
        self._name = name
        self._type = node_type
        self._image = image
        self._properties = dict(properties or {})
        self._visible = visible
        self._color_model = color_model
        self._color_depth = color_depth
        self._color_profile = color_profile or ""
        self._children = list(children or [])

    def name(self) -> str:
        return self._name

    def type(self) -> str:
        return self._type

    def property(self, key: str):
        return self._properties.get(key)

    def childNodes(self) -> List["_FakeNode"]:
        return list(self._children)

    def addChildNode(self, node: "_FakeNode", _after) -> None:
        self._children.append(node)

    def visible(self) -> bool:
        return self._visible

    def colorModel(self) -> str:
        return self._color_model

    def colorDepth(self) -> str:
        return self._color_depth

    def colorProfile(self) -> str:
        return self._color_profile

    def setColorSpace(self, color_model: str, color_depth: str, color_profile: str) -> None:
        if self._image is not None:
            target_format = None
            if color_model == "RGBA" and color_depth == "U8":
                target_format = FORMAT_RGBA8888
            elif color_model == "GRAY" and color_depth == "U16":
                target_format = FORMAT_GRAYSCALE16
            elif color_model == "GRAY" and color_depth == "U8":
                target_format = FORMAT_GRAYSCALE8
            if target_format is not None:
                self._image = self._image.convertToFormat(target_format)
        self._color_model = color_model
        self._color_depth = color_depth
        self._color_profile = color_profile

    def duplicate(self) -> "_FakeNode":
        dup_image = self._image.copy() if self._image is not None else None
        return _FakeNode(
            self._name,
            node_type=self._type,
            image=dup_image,
            properties=self._properties,
            visible=self._visible,
            color_model=self._color_model,
            color_depth=self._color_depth,
            color_profile=self._color_profile,
        )

    @builtins.property
    def image(self) -> Optional[QImage]:
        return self._image


class _FakeRootNode(_FakeNode):
    def __init__(self) -> None:
        super().__init__("root", node_type="root", properties={}, children=[])


class _FakeDocument:
    def __init__(
        self,
        width: int,
        height: int,
        name: str,
        color_model: str,
        color_depth: str,
        color_profile: str,
        resolution: float,
        file_name: str = "",
    ) -> None:
        self._width = width
        self._height = height
        self._name = name
        self._color_model = color_model
        self._color_depth = color_depth
        self._color_profile = color_profile
        self._resolution = resolution
        self._file_name = file_name
        self._root = _FakeRootNode()
        self._batch = False

    def rootNode(self) -> _FakeNode:
        return self._root

    def add_group(self, group: _FakeNode) -> None:
        self._root.addChildNode(group, None)

    def width(self) -> int:
        return self._width

    def height(self) -> int:
        return self._height

    def name(self) -> str:
        return self._name

    def fileName(self) -> str:
        return self._file_name

    def colorProfile(self) -> str:
        return self._color_profile

    def resolution(self) -> float:
        return self._resolution

    def setBatchmode(self, value: bool) -> None:
        self._batch = value

    def refreshProjection(self) -> None:
        pass

    def waitForDone(self) -> None:
        pass

    def setColorSpace(self, color_model: str, color_depth: str, color_profile: str) -> None:
        for child in self._root.childNodes():
            child.setColorSpace(color_model, color_depth, color_profile)

    def exportImage(self, path: str, _info: _FakeInfoObject) -> bool:
        nodes = [node for node in self._root.childNodes() if node.image is not None]
        if not nodes:
            return False
        image = nodes[-1].image
        if image is None:
            return False
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if not image.save(path):
            return False
        profile = nodes[-1].colorProfile()
        if profile and "sRGB" in profile:
            with Image.open(path) as img:
                icc_profile = ImageCms.ImageCmsProfile(ImageCms.createProfile("sRGB")).tobytes()
                img.save(path, icc_profile=icc_profile)
        return True

    def close(self) -> None:
        pass


class _FakeKritaSingleton:
    def createDocument(
        self,
        width: int,
        height: int,
        name: str,
        color_model: str,
        color_depth: str,
        color_profile: str,
        resolution: float,
    ) -> _FakeDocument:
        return _FakeDocument(width, height, name, color_model, color_depth, color_profile, resolution)


class PainterlyExporterIntegrationTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.output_dir = os.path.join(self._tmp.name, "export")
        os.makedirs(self.output_dir, exist_ok=True)

        self.document = self._build_document()
        self.export_options = exporter.ExportOptions(
            document=self.document,
            output_dir=self.output_dir,
            prefix="Sample_",
            flip_green=False,
            bake_normals=False,
            pack_orm=False,
            height_extension=".png",
            height_depth="U16",
            roughness_extension=".png",
            roughness_depth="U8",
        )

        self._original_krita = exporter.Krita
        self._original_info = exporter.InfoObject
        self._krita_singleton = _FakeKritaSingleton()
        exporter.Krita = type("KritaStub", (), {"instance": staticmethod(lambda: self._krita_singleton)})
        exporter.InfoObject = _FakeInfoObject
        self.addCleanup(self._restore_singletons)

        self.extension = exporter.PainterlyPbrExporterExtension(exporter.Krita.instance())

    def _restore_singletons(self) -> None:
        exporter.Krita = self._original_krita
        exporter.InfoObject = self._original_info

    def _build_document(self) -> _FakeDocument:
        width, height = 8, 8
        kra_path = os.path.join(self._tmp.name, "sample.kra")
        self._write_stub_kra(kra_path)
        document = _FakeDocument(width, height, "SampleDoc", "RGBA", "U8", "sRGB", 300.0, kra_path)

        material_group = _FakeNode(
            "PainterlyMaterial",
            node_type="grouplayer",
            properties={"materialGroup": True},
            color_model="RGBA",
            color_depth="U8",
            color_profile="sRGB",
            children=[],
        )

        base_layer = _FakeNode(
            "BaseColor",
            image=self._build_base_color(width, height),
            properties={"materialChannel": "BaseColor"},
            color_model="RGBA",
            color_depth="U8",
            color_profile="sRGB",
        )
        height_layer = _FakeNode(
            "Height",
            image=self._build_height_map(width, height),
            properties={"materialChannel": "Height"},
            color_model="GRAY",
            color_depth="U16",
            color_profile="linear",
        )
        normal_layer = _FakeNode(
            "Normal",
            image=self._build_normal_map(width, height),
            properties={"materialChannel": "Normal"},
            color_model="RGBA",
            color_depth="U8",
            color_profile="linear",
        )
        roughness_layer = _FakeNode(
            "Roughness",
            image=self._build_constant_map(width, height, 0.4),
            properties={"materialChannel": "Roughness"},
            color_model="GRAY",
            color_depth="U8",
            color_profile="linear",
        )
        metallic_layer = _FakeNode(
            "Metallic",
            image=self._build_constant_map(width, height, 0.1),
            properties={"materialChannel": "Metallic"},
            color_model="GRAY",
            color_depth="U8",
            color_profile="linear",
        )

        for child in (base_layer, height_layer, normal_layer, roughness_layer, metallic_layer):
            material_group.addChildNode(child, None)
        document.add_group(material_group)
        return document

    def _write_stub_kra(self, path: str) -> None:
        doc_xml = """<?xml version='1.0' encoding='UTF-8'?>
<Krita>
  <Document>
    <Layer name='PainterlyMaterial' materialGroup='true'>
      <Layer name='BaseColor' channel='BaseColor'/>
      <Layer name='Height' channel='Height'/>
      <Layer name='Normal' channel='Normal'/>
      <Layer name='Roughness' channel='Roughness'/>
      <Layer name='Metallic' channel='Metallic'/>
    </Layer>
  </Document>
</Krita>
"""
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("maindoc.xml", doc_xml)

    def _build_base_color(self, width: int, height: int) -> QImage:
        image = QImage(width, height, FORMAT_RGBA8888)
        for y in range(height):
            for x in range(width):
                color = QColor(180 + (x % 2) * 30, 100 + (y % 2) * 40, 60, 255)
                image.setPixelColor(x, y, color)
        return image

    def _build_height_map(self, width: int, height: int) -> QImage:
        image = QImage(width, height, FORMAT_GRAYSCALE16)
        for y in range(height):
            for x in range(width):
                value = 20000 + (x + y) * 500
                image.setPixel(x, y, value)
        return image

    def _build_normal_map(self, width: int, height: int) -> QImage:
        image = QImage(width, height, FORMAT_RGBA8888)
        purple = QColor(128, 128, 255, 255)
        image.fill(purple)
        return image

    def _build_constant_map(self, width: int, height: int, value: float) -> QImage:
        image = QImage(width, height, FORMAT_GRAYSCALE8)
        gray_value = int(max(0.0, min(1.0, value)) * 255)
        image.fill(gray_value)
        return image

    def test_export_produces_pbr_channels(self) -> None:
        entries = self.extension._export_document(self.export_options)
        self.assertEqual(len(entries), 1)
        entry = entries[0]
        self.extension._write_manifest(self.export_options, entries)

        base_path = os.path.join(self.export_options.output_dir, entry.files["BaseColor"])
        height_path = os.path.join(self.export_options.output_dir, entry.files["Height"])
        normal_path = os.path.join(self.export_options.output_dir, entry.files["Normal"])
        roughness_path = os.path.join(self.export_options.output_dir, entry.files["Roughness"])
        metallic_path = os.path.join(self.export_options.output_dir, entry.files["Metallic"])

        self.assertTrue(os.path.exists(base_path))
        self.assertTrue(os.path.exists(height_path))
        self.assertTrue(os.path.exists(normal_path))
        self.assertTrue(os.path.exists(roughness_path))
        self.assertTrue(os.path.exists(metallic_path))

        with Image.open(base_path) as base_image:
            self.assertEqual(base_image.mode, "RGBA")
            self.assertIn("icc_profile", base_image.info)

        with Image.open(height_path) as height_image:
            self.assertEqual(height_image.mode, "I;16")
            arr = np.array(height_image, dtype=np.float32) / 65535.0
            self.assertGreater(arr.mean(), 0.2)
            self.assertLess(arr.mean(), 0.6)

        with Image.open(normal_path) as normal_image:
            normal_rgb = np.array(normal_image.convert("RGB"), dtype=np.float32) / 255.0
            vectors = normal_rgb * 2.0 - 1.0
            lengths = np.linalg.norm(vectors, axis=2)
            self.assertTrue(np.all(lengths > 0.6))
            self.assertTrue(np.all(lengths < 1.4))
            self.assertGreater(normal_rgb[..., 2].mean(), 0.7)

        with Image.open(roughness_path) as roughness_image:
            rough = np.array(roughness_image, dtype=np.float32) / 255.0
            self.assertGreaterEqual(rough.min(), 0.0)
            self.assertLessEqual(rough.max(), 1.0)

        with Image.open(metallic_path) as metallic_image:
            metallic = np.array(metallic_image, dtype=np.float32) / 255.0
            self.assertGreaterEqual(metallic.min(), 0.0)
            self.assertLessEqual(metallic.max(), 1.0)

        manifest_path = os.path.join(self.export_options.output_dir, "material.yaml")
        self.assertTrue(os.path.exists(manifest_path))


if __name__ == "__main__":
    unittest.main()
