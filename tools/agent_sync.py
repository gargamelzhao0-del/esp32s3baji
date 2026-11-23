"""Pair the physical badge or publish generic Agent, BAJI v1 and Codex JSONL."""
import argparse
import asyncio
import getpass
import json
from pathlib import Path
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"host"))
from baji_bridge.device import DeviceState
from baji_bridge.pairing import configure_station, pair
from baji_bridge.publisher import stream_events


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest="cmd", required=True)
    scan = subs.add_parser("scan", help="list nearby badges advertising the BAJI Bluetooth service")
    scan.add_argument("--seconds", type=float, default=5)
    pairing = subs.add_parser("pair",help="save credentials from a badge connected by USB")
    pairing.add_argument("--port",required=True)
    pairing.add_argument("--show-ap",action="store_true",help="print the private access-point passphrase")
    wifi = subs.add_parser("network",help="save next-boot Wi-Fi settings over USB")
    wifi.add_argument("--port",required=True)
    mode = wifi.add_mutually_exclusive_group(required=True)
    mode.add_argument("--ssid")
    mode.add_argument("--ap",action="store_true")
    sender = subs.add_parser("send",help="publish JSONL from stdin or a file")
    radio = sender.add_mutually_exclusive_group(required=True)
    radio.add_argument("--wifi",metavar="URL")
    radio.add_argument("--ble",metavar="ADDRESS")
    sender.add_argument("--input",type=Path)
    sender.add_argument("--interval",type=float,default=0.,help="seconds between replay records")
    sender.add_argument("--model",help="model label for CLI events that omit it")
    for sub in (sender,pairing):
        sub.add_argument("--profile",type=Path,default=Path.home()/".baji"/"pairing.json")
    args = parser.parse_args()
    if args.cmd == "scan":
        from baji_bridge.transports import discover_badges
        badges = asyncio.run(discover_badges(args.seconds))
        for address, name in badges:
            print(f"{address}  {name}")
        if not badges:
            print("No advertising BAJI badge found.", file=sys.stderr)
    elif args.cmd == "pair":
        ssid, password = pair(args.port,args.profile)
        print(f"Paired {ssid}. Profile: {args.profile}")
        if args.show_ap:
            print(f"Wi-Fi SSID: {ssid}\nWi-Fi password: {password}")
    elif args.cmd == "network":
        configure_station(args.port,args.ssid or "",getpass.getpass("Wi-Fi passphrase: ") if args.ssid else "")
        print("Settings saved. Reboot the badge to use the new network.")
    else:
        from baji_bridge.transports import WifiTransport, BleTransport
        profile = json.loads(args.profile.expanduser().read_text(encoding="utf8"))
        key = bytes.fromhex(profile["key"])
        if len(key)!=32 or not 0 <= args.interval <= 60:
            raise ValueError("invalid profile key length or replay interval")
        transport = (lambda: WifiTransport(args.wifi)) if args.wifi else (lambda: BleTransport(args.ble))
        state = DeviceState(model=args.model)
        if args.input:
            with args.input.open("rb") as source:
                revision = asyncio.run(stream_events(source,state,transport,key,args.interval))
        else:
            revision = asyncio.run(stream_events(sys.stdin.buffer,state,transport,key,args.interval))
        print(f"Badge acknowledged state revision {revision}.", file=sys.stderr)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, ImportError, KeyError) as exc:
        sys.exit(str(exc))
