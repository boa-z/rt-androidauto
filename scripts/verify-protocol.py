#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare the bounded schema against pinned upstream Python protobuf types.

This is an independent schema oracle, supplementary to the C/nanopb tests.
It does not establish phone interoperability or validate untested messages.
"""
import argparse
import importlib
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--upstream", type=Path, required=True)
parser.add_argument("--output", type=Path, default=root / "build/protocol-oracle")
args = parser.parse_args()
sha = subprocess.check_output(["git", "-C", str(args.upstream), "rev-parse", "HEAD"], text=True).strip()
if sha != "61eab61c5f9968154ff1a80faa8c0a427b208479":
    raise SystemExit("Expected the reviewed Open Android Auto SHA; got " + sha)
files = set()
def collect(name):
    if name in files:
        return
    files.add(name)
    text = (args.upstream / name).read_text(encoding="utf-8")
    for dependency in re.findall(r'^import\s+"([^"]+)"', text, re.M):
        collect(dependency)

for name in ["control/ChannelOpenRequestMessage", "audio/AudioConfigData",
             "av/AVChannelSetupRequestMessage", "av/AVChannelStartIndicationMessage",
             "input/TouchLocationData", "video/VideoConfigData",
             "navigation/NavigationFocusRequestMessage", "navigation/NavigationFocusResponseMessage",
             "control/VoiceSessionRequestMessage", "control/BatteryStatusMessage",
             "control/CallAvailabilityMessage", "input/InputBindingRequestMessage",
             "input/InputChannelConfigData", "input/RelativeInputEventData"]:
    collect("oaa/" + name + ".proto")
args.output.mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable, "-m", "grpc_tools.protoc", "-I", str(args.upstream),
                "--python_out=" + str(args.output), *sorted(files)], check=True)
subprocess.run([sys.executable, "-m", "grpc_tools.protoc", "-I", str(root / "proto"),
                "--python_out=" + str(args.output), str(root / "proto/aap.proto")], check=True)
sys.path.insert(0, str(args.output.resolve()))
ours = importlib.import_module("aap_pb2")
def upstream(name, cls):
    return getattr(importlib.import_module("oaa." + name + "_pb2"), cls)

pairs = [
    (ours.ChannelOpen(priority=-13, channel_id=197), upstream("control.ChannelOpenRequestMessage", "ChannelOpenRequest")(priority=-13, channel_id=197)),
    (ours.AudioConfig(sample_rate=48000, bit_depth=16, channel_count=2), upstream("audio.AudioConfigData", "AudioConfig")(sample_rate=48000, bit_depth=16, channel_count=2)),
    (ours.SetupRequest(codec=3), upstream("av.AVChannelSetupRequestMessage", "AVChannelSetupRequest")(media_codec_type=3)),
    (ours.Start(session=42, config=0), upstream("av.AVChannelStartIndicationMessage", "AVChannelStartIndication")(session=42, config=0)),
    (ours.TouchLocation(x=256, y=60, pointer_id=0), upstream("input.TouchLocationData", "TouchLocation")(x=256, y=60, pointer_id=0)),
    (ours.VideoConfig(video_resolution=2, video_fps=1, margin_width=512, margin_height=120, dpi=160), upstream("video.VideoConfigData", "VideoConfig")(video_resolution=2, video_fps=1, margin_width=512, margin_height=120, dpi=160)),
    (ours.NavigationFocus(type=2), upstream("navigation.NavigationFocusRequestMessage", "NavigationFocusRequest")(type=2)),
    (ours.NavigationFocus(type=1), upstream("navigation.NavigationFocusResponseMessage", "NavigationFocusResponse")(type=1)),
    (ours.VoiceSession(session_type=1), upstream("control.VoiceSessionRequestMessage", "VoiceSessionRequest")(session_type=1)),
    (ours.BatteryStatus(battery_level=0, time_remaining_s=60, critical_battery=True), upstream("control.BatteryStatusMessage", "BatteryStatusNotification")(battery_level=0, time_remaining_s=60, critical_battery=True)),
    (ours.CallAvailability(call_available=False), upstream("control.CallAvailabilityMessage", "CallAvailabilityStatus")(call_available=False)),
    (ours.Binding(keycodes=[4, 22, 85, 65536]), upstream("input.InputBindingRequestMessage", "InputBindingRequest")(keycodes=[4, 22, 85, 65536])),
    (ours.InputChannel(keycodes=[4, 22, 85, 65536]), upstream("input.InputChannelConfigData", "InputChannelConfig")(supported_keycodes=[4, 22, 85, 65536])),
    (ours.RelativeInput(scan_code=65536, delta=-3), upstream("input.RelativeInputEventData", "RelativeInputEvent")(scan_code=65536, delta=-3)),
]
for a, b in pairs:
    assert a.SerializeToString() == b.SerializeToString(), (a, b)
    b.ParseFromString(a.SerializeToString())
    a.ParseFromString(b.SerializeToString())
    assert a.IsInitialized() and b.IsInitialized()
print("upstream protobuf interoperability: PASS (" + str(len(pairs)) + " message fixtures, SHA " + sha + ")")
