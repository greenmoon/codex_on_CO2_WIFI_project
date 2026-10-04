"""Generate true SVG QR codes for the Windows MQTT Bridge."""
import argparse
from pathlib import Path
import qrcode
from qrcode.image.svg import SvgPathImage

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
OFFLINE_QR = ROOT / "offline_qr"

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--url", default="http://192.168.1.3:8080/", help="iPhone-reachable Dashboard URL")
args = parser.parse_args()

OFFLINE_QR.mkdir(exist_ok=True)
for output in (DATA / "qr_windows_dashboard.svg", OFFLINE_QR / "windows_mqtt_dashboard.svg"):
    qr = qrcode.QRCode(box_size=8, border=4)
    qr.add_data(args.url)
    qr.make(fit=True)
    qr.make_image(image_factory=SvgPathImage).save(str(output))
    print(f"{output.relative_to(ROOT)}: {args.url}")
