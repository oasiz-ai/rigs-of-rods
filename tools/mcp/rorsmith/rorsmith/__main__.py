"""Entry point: `rorsmith` (stdio MCP server) or `rorsmith --selftest`."""

from __future__ import annotations

import asyncio
import json
import sys


def main() -> int:
    argv = sys.argv[1:]
    if argv and argv[0] in {"--selftest", "selftest"}:
        from .server import TOOLS

        print(
            json.dumps(
                {
                    "server": "rorsmith",
                    "transport": "stdio",
                    "tools": sorted(TOOLS),
                },
                indent=1,
            )
        )
        return 0
    if argv and argv[0] in {"--call"}:
        # rorsmith --call <tool> '<json args>' - for scripted verification.
        from .server import TOOLS

        name = argv[1]
        arguments = json.loads(argv[2]) if len(argv) > 2 else {}
        entry = TOOLS.get(name)
        if entry is None:
            print(json.dumps({"refused": True, "reason": "unknown_tool", "detail": name}))
            return 1
        from .paths import RorsmithError

        try:
            print(json.dumps(entry[1](arguments), indent=1, default=str))
        except RorsmithError as exc:
            print(json.dumps({"refused": True, "reason": exc.reason, "detail": exc.detail}, indent=1))
            return 1
        return 0
    from .server import run

    asyncio.run(run())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
