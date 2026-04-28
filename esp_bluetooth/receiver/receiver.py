import asyncio
from bleak import BleakClient, BleakScanner

SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
CHAR_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"


async def main():
    devices = await BleakScanner.discover()

    esp = None
    for d in devices:
        if d.name == "MyESP32":
            esp = d
            break

    if not esp:
        print("ESP32 not found")
        return

    async with BleakClient(esp.address) as client:
        print("Connected!")

        def callback(_, data):
            print("Received:", data.decode())

        await client.start_notify(CHAR_UUID, callback)

        while True:
            await asyncio.sleep(1)

asyncio.run(main())
