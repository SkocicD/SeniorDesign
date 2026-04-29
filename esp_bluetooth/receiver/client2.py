import asyncio
from bleak import BleakClient
import struct

DEVICE_ADDRESS = "82AD5258-7041-6CC5-F405-85FED1E838C9"
CHAR_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8"


def callback(sender, data):
    floats = struct.unpack('<16f', data)
    print(floats)


async def main():
    async with BleakClient(DEVICE_ADDRESS) as client:
        await client.start_notify(CHAR_UUID, callback)

        print("Listening for notifications...")
        await asyncio.sleep(60)

        await client.stop_notify(CHAR_UUID)

asyncio.run(main())
