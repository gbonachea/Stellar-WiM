#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Open a few real GTK Wayland windows for integration tests."""

import sys

import gi

gi.require_version("Gtk", "3.0")
from gi.repository import Gtk  # noqa: E402


def main() -> int:
    count = int(sys.argv[1])
    prefix = sys.argv[2] if len(sys.argv) > 2 else "stellar-test"
    app = Gtk.Application(application_id="org.stellarwim.IntegrationTest")
    app.register()
    for index in range(count):
        window = Gtk.ApplicationWindow(application=app)
        window.set_title(f"{prefix}-{index}")
        window.set_default_size(240, 180)
        window.show_all()
    app.run([])
    return 0


if __name__ == "__main__":
    sys.exit(main())
