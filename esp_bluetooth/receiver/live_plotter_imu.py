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

q = queue.Queue(maxsize=2000)

# ---------------- STATE ----------------
stop_event = threading.Event()

# ---------------- BLE ----------------


def callback(sender, data):
    if stop_event.is_set():
        return

    try:
        sample = list(struct.unpack("<6f", data))
        sample = [
            sample[0],
            sample[1],
            sample[2],
            sample[3]/30,
            sample[4]/30,
            sample[5]/30,
        ]
        q.put_nowait(sample)
    except queue.Full:
        pass


async def ble_loop():
    async with BleakClient(DEVICE_ADDRESS) as client:
        await client.start_notify(CHAR_UUID, callback)

        while not stop_event.is_set():
            await asyncio.sleep(0.1)

        await client.stop_notify(CHAR_UUID)


def run_ble():
    asyncio.run(ble_loop())


# ---------------- QT APP ----------------
app = QtWidgets.QApplication([])

win = pg.GraphicsLayoutWidget(title="BLE Stream (6ch)")
win.resize(900, 500)
win.show()

plot = win.addPlot()
plot.addLegend()

curves = []
for i in range(NUM_CHANNELS):
    c = plot.plot(pen=pg.intColor(i), name=f"ch{i}")
    curves.append(c)

data = np.zeros((NUM_CHANNELS, BUFFER_SIZE))


# ---------------- UPDATE LOOP ----------------
def update():
    global data

    updated = False

    while True:
        try:
            sample = q.get_nowait()
        except queue.Empty:
            break

        data[:, :-1] = data[:, 1:]
        data[:, -1] = sample
        updated = True

    if updated:
        for i in range(NUM_CHANNELS):
            curves[i].setData(data[i])


timer = QtCore.QTimer()
timer.timeout.connect(update)
timer.start(20)


# ---------------- CLEAN SHUTDOWN ----------------
def shutdown():
    print("Shutting down...")

    stop_event.set()   # stop BLE loop

    timer.stop()

    # let BLE thread exit
    ble_thread.join(timeout=2)

    app.quit()


# trigger shutdown when Qt quits
app.aboutToQuit.connect(shutdown)


# ---------------- START BLE THREAD ----------------
ble_thread = threading.Thread(target=run_ble, daemon=True)
ble_thread.start()


# ---------------- RUN ----------------
app.exec()
