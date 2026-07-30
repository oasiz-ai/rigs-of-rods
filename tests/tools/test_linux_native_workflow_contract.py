#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Static contract tests for the legacy Linux sysdeps workflow."""

from __future__ import annotations

from pathlib import Path
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = REPOSITORY_ROOT / ".github/workflows/linux-native.yml"
CONTENT_COMMIT = "34fefdd126784bf87b068fc283f812525d159dd7"


class LinuxNativeWorkflowContractTests(unittest.TestCase):
    def test_checkout_initializes_and_verifies_pinned_content(self) -> None:
        text = WORKFLOW.read_text(encoding="utf-8")
        checkout = """      - uses: actions/checkout@v7
        with:
          submodules: true

      - name: Verify pinned starter content
        run: |
          test "$(git -C content rev-parse HEAD)" = \\
            "34fefdd126784bf87b068fc283f812525d159dd7"
"""
        self.assertEqual(text.count(checkout), 1)
        self.assertEqual(text.count(CONTENT_COMMIT), 1)
        self.assertLess(
            text.index("Verify pinned starter content"),
            text.index("Build Rigs of Rods"),
        )


if __name__ == "__main__":
    unittest.main()
