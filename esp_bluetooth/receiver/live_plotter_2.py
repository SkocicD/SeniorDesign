import asyncio
import struct
import threading
import queue
import numpy as np
import pyqtgraph as pg
from pyqtgraph.Qt import QtWidgets, QtCore
from bleak import BleakClient

DEVICE_ADDRESS = "82AD5258-7041-6CC5-F405-85FED1E838C9"
CHAR_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8"

NUM_CHANNELS = 6
BUFFER_SIZE = 1000

# Thread-safe queue for incoming samples
q = queue.Queue(maxsize=1000)

# ---------------- BLE CALLBACK ----------------


def callback(sender, data):
    floats = struct.unpack('<6f', data)

    try:
        q.put_nowait(floats)
    except queue.Full:
        pass  # drop data if overwhelmed

# ---------------- BLE LOOP ----------------


async def ble_main():
    async with BleakClient(DEVICE_ADDRESS) as client:
        await client.start_notify(CHAR_UUID, callback)
        print("Listening for notifications...")
        while True:
            await asyncio.sleep(1)


def run_ble_loop():
    asyncio.run(ble_main())


# ---------------- PLOT SETUP ----------------
app = QtWidgets.QApplication([])
win = pg.GraphicsLayoutWidget(title="6 Channel BLE Stream")
win.show()

plot = win.addPlot(title="Channels")
curves = [plot.plot(pen=pg.intColor(i)) for i in range(NUM_CHANNELS)]

data = np.zeros((NUM_CHANNELS, BUFFER_SIZE))

# ---------------- UPDATE LOOP ----------------


def update():
    global data

    updated = False

    while not q.empty():
        sample = q.get()

        data[:, :-1] = data[:, 1:]
        data[:, -1] = sample
        updated = True

    if updated:
        for i in range(NUM_CHANNELS):
            curves[i].setData(data[i], skipFiniteCheck=True)


timer = QtCore.QTimer()
timer.timeout.connect(update)
timer.start(20)  # ~50 FPS GUI update

# ---------------- START THREAD ----------------
ble_thread = threading.Thread(target=run_ble_loop, daemon=True)
ble_thread.start()

# ---------------- RUN GUI ----------------
app.exec()
