import asyncio
from bleak import BleakClient

# replace with your ESP32 MAC
DEVICE_ADDRESS = "82AD5258-7041-6CC5-F405-85FED1E838C9"
CHAR_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8"


async def main():
    async with BleakClient(DEVICE_ADDRESS) as client:
        print("Connected:", client.is_connected)

        # Read value
        value = await client.read_gatt_char(CHAR_UUID)
        print("Raw:", value)
        print("String:", value.decode("utf-8"))

asyncio.run(main())
