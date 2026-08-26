#!/usr/bin/env python3
"""Drive rorsmith over real stdio MCP, the way Claude Code does.

`rorsmith --selftest` and `--call` never construct the server, so they cannot
catch a break in the MCP wiring itself. This does: it spawns the server exactly
as the repo-root .mcp.json declares it, completes `initialize`, enumerates the
tools, and makes one read-only call.

    uv run --directory tools/mcp/rorsmith python selftest_protocol.py

Exits non-zero on any failure.
"""

from __future__ import annotations

import asyncio
import json
import os
import sys
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

PACKAGE_DIR = Path(__file__).resolve().parent
REPO_ROOT = PACKAGE_DIR.parents[2]
EXPECTED_TOOLS = {
    "apply_to_archive",
    "author_layers",
    "derive_normal_map",
    "derive_roughness",
    "fit_generator",
    "generate_material",
    "inspect_material",
    "list_generators",
    "list_materials",
    "renderer_policy",
    "verify_live",
}


def server_parameters() -> StdioServerParameters:
    config = json.loads((REPO_ROOT / ".mcp.json").read_text())["mcpServers"]["rorsmith"]
    project = os.environ.get("CLAUDE_PROJECT_DIR", str(REPO_ROOT))

    def expand(value: str) -> str:
        return value.replace("${CLAUDE_PROJECT_DIR:-.}", project)

    return StdioServerParameters(
        command=config["command"],
        args=[expand(argument) for argument in config["args"]],
        env={**os.environ, **{k: expand(v) for k, v in config.get("env", {}).items()}},
    )


async def main() -> int:
    async with stdio_client(server_parameters()) as (read, write):
        async with ClientSession(read, write) as session:
            await session.initialize()
            names = {tool.name for tool in (await session.list_tools()).tools}
            missing = EXPECTED_TOOLS - names
            if missing:
                print(f"FAIL: missing tools {sorted(missing)}", file=sys.stderr)
                return 1
            result = await session.call_tool("renderer_policy", {})
            policy = json.loads(result.content[0].text)
            if policy.get("refused"):
                print(f"FAIL: renderer_policy refused: {policy}", file=sys.stderr)
                return 1
            for key in ("refusal_tokens", "roughness_bands", "structural_limits"):
                if not policy.get(key):
                    print(f"FAIL: renderer_policy has no {key}", file=sys.stderr)
                    return 1
            print(
                f"ok: {len(names)} tools, "
                f"{len(policy['refusal_tokens'])} refusal tokens, "
                f"{len(policy['roughness_bands'])} roughness bands, "
                f"repo_root={policy['repo_root']}"
            )
            return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
