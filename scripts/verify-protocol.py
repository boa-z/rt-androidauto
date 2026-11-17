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
             "input/InputChannelConfigData", "input/RelativeInputEventData",
             "media/MediaPlaybackStatusMessage", "media/MediaPlaybackMetadataMessage",
             "control/ChannelDescriptorData", "phone/PhoneStatusMessage",
             "phone/PhoneStatusInputMessage", "navigation/NavigationStateMessage",
             "navigation/NavigationNotificationMessage", "navigation/NavigationTurnEventMessage",
             "navigation/InstrumentClusterMessages"]:
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
    (ours.MediaStatus(state=2, source_app="YouTube Music", position_seconds=80, shuffle=False, repeat=False, repeat_one=False), upstream("media.MediaPlaybackStatusMessage", "MediaPlaybackStatus")(playback_state=2, source_app="YouTube Music", position_seconds=80, shuffle=False, repeat=False, repeat_one=False)),
    (ours.MediaMetadata(title="", artist="音乐家", album="Album"), upstream("media.MediaPlaybackMetadataMessage", "MediaPlaybackMetadata")(title="", artist="音乐家", album="Album")),
    (ours.Channel(channel_id=155, media_info=ours.Empty()), upstream("control.ChannelDescriptorData", "ChannelDescriptor")(channel_id=155, media_info_channel=upstream("media.MediaChannelData", "MediaInfoChannel")())),
]
phone_module = "phone.PhoneStatusMessage"
nav_module = "navigation.NavigationNotificationMessage"
distance_module = "navigation.NavigationTurnEventMessage"
pairs += [
    (ours.Channel(channel_id=163, navigation=ours.NavigationChannel(minimum_interval_ms=1500, type=1)), upstream("control.ChannelDescriptorData", "ChannelDescriptor")(channel_id=163, navigation_channel=upstream("navigation.NavigationChannelData", "NavigationChannel")(minimum_interval_ms=1500, type=1))),
    (ours.Channel(channel_id=229, phone=ours.Empty()), upstream("control.ChannelDescriptorData", "ChannelDescriptor")(channel_id=229, phone_status_channel=upstream("control.ChannelDescriptorData", "PhoneStatusChannel")())),
    (ours.PhoneStatus(calls=[ours.PhoneCall(state=4, duration_seconds=0, number="+123", display_name="")], signal_strength=0), upstream(phone_module, "PhoneStatusUpdate")(calls=[upstream(phone_module, "PhoneCall")(call_state=4, call_duration_seconds=0, phone_number="+123", display_name="")], signal_strength=0)),
    (ours.PhoneInput(input_type=ours.PhoneInputType(action=7), caller_id="", display_name="Caller"), upstream("phone.PhoneStatusInputMessage", "PhoneStatusInput")(input_type=upstream("phone.PhoneStatusInputMessage", "PhoneInputType")(action=7), caller_id="", display_name="Caller")),
    (ours.NavigationState(state=3), upstream("navigation.NavigationStateMessage", "NavigationState")(state=3)),
    (ours.NavigationNotification(steps=[ours.NavStep(maneuver=ours.NavManeuver(type=7, exit_number=0, turn_angle=-90), instruction=ours.NavText(text="Turn left"), lanes=[ours.NavLane(directions=[ours.NavDirection(shape=4, recommended=False), ours.NavDirection()])], road_info=ours.NavRoadInfo(road_names=["Road"]))], destinations=[ours.NavDestination(address="", charging_station=ours.NavChargingStation(connector_count=2, available_count=0, max_power_kw=50))]),
     upstream(nav_module, "NavigationNotification")(steps=[upstream(nav_module, "NavigationStep")(maneuver=upstream(nav_module, "NavigationManeuver")(type=7, roundabout_exit_number=0, roundabout_turn_angle=-90), instruction=upstream(nav_module, "NavigationText")(text="Turn left"), lanes=[upstream(nav_module, "NavigationLane")(directions=[upstream(nav_module, "NavigationLaneDirection")(shape=4, is_recommended=False), upstream(nav_module, "NavigationLaneDirection")()])], road_info=upstream(nav_module, "NavigationRoadInfo")(road_names=["Road"]))], destinations=[upstream(nav_module, "NavigationDestination")(address="", charging_station=upstream(nav_module, "ChargingStationDetails")(connector_count=2, available_count=0, max_power_kw=50))])),
    (ours.NavigationPosition(step_distance=ours.NavStepDistance(distance=ours.NavDistance(value=0, display_text="0", unit=1), time_to_step_seconds=5000000000), destinations=[ours.NavDestinationDistance(estimated_time_of_arrival="12:30", time_to_arrival_seconds=5000000001)], current_road=ours.NavText(text="")),
     upstream(distance_module, "NavigationNextTurnDistanceEvent")(step_distance=upstream(distance_module, "NavigationStepDistance")(distance=upstream(distance_module, "NavigationTurnDistance")(distance_value=0, display_text="0", distance_unit=1), time_to_step_seconds=5000000000), destination_distances=[upstream(distance_module, "DestinationDistance")(estimated_time_of_arrival="12:30", time_to_arrival_seconds=5000000001)], current_road=upstream(nav_module, "NavigationText")(text=""))),
    (ours.NavigationTurn(road_name="Road", maneuver_type=7, turn_direction=1, distance_meters=0, distance_unit=1), upstream(distance_module, "NavigationTurnEvent")(road_name="Road", maneuver_type=7, turn_direction=1, distance_meters=0, distance_unit=1)),
    (ours.Empty(), upstream("navigation.InstrumentClusterMessages", "InstrumentClusterStart")()),
    (ours.Empty(), upstream("navigation.InstrumentClusterMessages", "InstrumentClusterStop")()),
]
for a, b in pairs:
    assert a.SerializeToString() == b.SerializeToString(), (a, b)
    b.ParseFromString(a.SerializeToString())
    a.ParseFromString(b.SerializeToString())
    assert a.IsInitialized() and b.IsInitialized()
print("upstream protobuf interoperability: PASS (" + str(len(pairs)) + " message fixtures, SHA " + sha + ")")
