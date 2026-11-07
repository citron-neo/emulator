#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract the current fence builders; fail rather than silently benchmark stale copies."""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parents[2]
output = pathlib.Path(sys.argv[1])
header = (root / "src/video_core/dma_pusher.h").read_text()
source = (root / "src/core/hle/service/nvdrv/devices/nvhost_gpu.cpp").read_text()


def definition(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


command_list = definition(header, "struct CommandList final") + ";"
if command_list.count("std::vector<") != 3:
    raise RuntimeError("CommandList storage changed; update the baseline comparison")
action = definition(source, "Tegra::CommandHeader BuildFenceAction(")
names = ["BuildWaitCommandList", "BuildIncrementCommandList", "BuildIncrementWithWfiCommandList"]
builders = "\n".join(definition(source, "static std::vector<Tegra::CommandHeader> " + name)
                     for name in names)
# The original Eden-4399 baseline used the same builders and 512 inline entries.
legacy_list = re.sub(r"std::vector<([^>]+)>", r"boost::container::small_vector<\1, 512>", command_list)
legacy_builders = builders.replace("std::vector<Tegra::CommandHeader>",
                                   "boost::container::small_vector<Tegra::CommandHeader, 512>")
output.write_text("namespace Legacy {\nusing namespace Tegra;\n" + legacy_list + "\n}\n"
                  + "namespace CurrentBuilders {\n" + action + "\n" + builders + "\n}\n"
                  + "namespace LegacyBuilders {\n" + action + "\n" + legacy_builders + "\n}\n")
