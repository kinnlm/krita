# SPDX-License-Identifier: GPL-3.0-or-later

try:  # pragma: no cover - Krita runtime is optional for tests
    import krita  # type: ignore[import-not-found]
except ImportError:  # pragma: no cover - headless environment
    krita = None  # type: ignore[assignment]

from .exporter import PainterlyPbrExporterExtension

if krita is not None and hasattr(krita, "Scripter"):
    krita.Scripter.addExtension(PainterlyPbrExporterExtension(krita.Krita.instance()))

