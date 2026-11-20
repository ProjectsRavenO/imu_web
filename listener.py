import asyncio
import struct
import time

from bleak import BleakScanner, BleakClient
import matplotlib.pyplot as plt


# ============================================================
# BLE
# ============================================================

DEVICE_NAME = "IMU_Sense"

DATA_UUID = "19B10001-E8F2-537E-4F6C-D104768A1214"
CMD_UUID  = "19B10002-E8F2-537E-4F6C-D104768A1214"


# ============================================================
# PACKET FORMAT
# ============================================================

# C++:
#
# struct AccelSample {
#     uint32_t timestamp;
#     int16_t ax;
#     int16_t ay;
#     int16_t az;
# };
#
# 4 + 2 + 2 + 2 = 10 bytes

SAMPLE_SIZE = 10
SAMPLES_PER_PACKET = 20
PACKET_SIZE = SAMPLE_SIZE * SAMPLES_PER_PACKET


# ============================================================
# TIMESTAMP
# ============================================================

# Arduino micros() gives microseconds
TIMESTAMP_MODULO = 1 << 32

# Expected sample period at 416 Hz
EXPECTED_DT = 1.0 / 416.0
EXPECTED_DT_US = EXPECTED_DT * 1_000_000.0


# ============================================================
# DATA STORAGE
# ============================================================

timestamps = []

ax_data = []
ay_data = []
az_data = []

dt_data = []

packet_count = 0
sample_count = 0

first_timestamp = None
previous_timestamp = None


# ============================================================
# TIMESTAMP HANDLING
# ============================================================

def timestamp_delta(current, previous):
    """
    Calculate difference between two uint32_t micros()
    timestamps.

    Handles the 32-bit micros() counter wrapping around.
    """

    return (current - previous) & 0xFFFFFFFF


def timestamp_to_seconds(timestamp_us):
    """
    Convert Arduino micros() timestamp to seconds.
    """

    return timestamp_us / 1_000_000.0


# ============================================================
# BLE NOTIFICATION
# ============================================================

def notification_handler(sender, data):

    global packet_count
    global sample_count
    global first_timestamp
    global previous_timestamp

    if len(data) != PACKET_SIZE:

        print(
            f"\nWARNING: received {len(data)} bytes "
            f"(expected {PACKET_SIZE})"
        )

        return

    packet_count += 1

    packet_first_timestamp = None
    packet_last_timestamp = None

    packet_dt = []

    # ========================================================
    # Decode all samples
    # ========================================================

    for i in range(SAMPLES_PER_PACKET):

        offset = i * SAMPLE_SIZE

        timestamp, ax, ay, az = struct.unpack_from(
            "<Ihhh",
            data,
            offset
        )

        # ----------------------------------------------------
        # First timestamp
        # ----------------------------------------------------

        if first_timestamp is None:

            first_timestamp = timestamp


        # ----------------------------------------------------
        # Calculate dt
        # ----------------------------------------------------

        if previous_timestamp is not None:

            delta_us = timestamp_delta(
                timestamp,
                previous_timestamp
            )

            delta_seconds = delta_us / 1_000_000.0

            dt_data.append(delta_seconds)
            packet_dt.append(delta_us)

        previous_timestamp = timestamp


        # ----------------------------------------------------
        # Relative time from first sample
        # ----------------------------------------------------

        relative_us = timestamp_delta(
            timestamp,
            first_timestamp
        )

        relative_time = relative_us / 1_000_000.0

        timestamps.append(relative_time)


        # ----------------------------------------------------
        # Acceleration
        # ----------------------------------------------------

        ax_data.append(ax / 1000.0)
        ay_data.append(ay / 1000.0)
        az_data.append(az / 1000.0)

        sample_count += 1


        # ----------------------------------------------------
        # Packet timestamps
        # ----------------------------------------------------

        if packet_first_timestamp is None:
            packet_first_timestamp = timestamp

        packet_last_timestamp = timestamp


    # ========================================================
    # PRINT PACKET INFORMATION
    # ========================================================

    print(
        f"Packet {packet_count:4d} | "
        f"Samples {sample_count:5d} | "
        f"Timestamp "
        f"{packet_first_timestamp:10d} -> "
        f"{packet_last_timestamp:10d}",
        end=""
    )


    # ========================================================
    # TIMING STATISTICS
    # ========================================================

    if packet_dt:

        avg_dt = sum(packet_dt) / len(packet_dt)

        minimum_dt = min(packet_dt)
        maximum_dt = max(packet_dt)

        print(
            f" | dt avg {avg_dt:7.1f} us "
            f"| min {minimum_dt:7.1f} "
            f"| max {maximum_dt:7.1f}"
        )

    else:

        print()


# ============================================================
# MAIN
# ============================================================

async def main():

    print()
    print("========================================")
    print("        IMU BLE TEST PROGRAM")
    print("========================================")
    print()

    print("Expected sample rate : 416 Hz")
    print(f"Expected sample dt   : {EXPECTED_DT_US:.2f} us")
    print("Timestamp source     : Arduino micros()")
    print()

    print("Scanning for IMU...")

    device = await BleakScanner.find_device_by_name(
        DEVICE_NAME,
        timeout=10
    )

    if device is None:

        print()
        print("Could not find IMU_Sense")
        return

    print(f"Found:   {device.name}")
    print(f"Address: {device.address}")
    print()

    async with BleakClient(device) as client:

        print("Connecting...")

        print("Connected!")
        print()

        await client.start_notify(
            DATA_UUID,
            notification_handler
        )

        print("Notifications enabled.")
        print()
        print("Commands:")
        print("  start  -> start recording")
        print("  stop   -> stop recording")
        print("  quit   -> disconnect")
        print()

        while True:

            command = await asyncio.to_thread(
                input,
                "> "
            )

            command = command.strip().lower()


            # =================================================
            # START
            # =================================================

            if command == "start":

                # Reset Python-side data

                timestamps.clear()

                ax_data.clear()
                ay_data.clear()
                az_data.clear()

                dt_data.clear()

                global packet_count
                global sample_count
                global first_timestamp
                global previous_timestamp

                packet_count = 0
                sample_count = 0

                first_timestamp = None
                previous_timestamp = None

                await client.write_gatt_char(
                    CMD_UUID,
                    b"START",
                    response=False
                )

                print()
                print("START sent")
                print("Recording...")
                print()


            # =================================================
            # STOP
            # =================================================

            elif command == "stop":

                await client.write_gatt_char(
                    CMD_UUID,
                    b"STOP",
                    response=False
                )

                print()
                print("STOP sent")
                print()


            # =================================================
            # QUIT
            # =================================================

            elif command == "quit":

                await client.write_gatt_char(
                    CMD_UUID,
                    b"STOP",
                    response=False
                )

                print()
                print("STOP sent")

                break


            else:

                print(
                    "Unknown command. "
                    "Use start, stop, or quit."
                )


        await client.stop_notify(DATA_UUID)


    # ========================================================
    # RECORDING SUMMARY
    # ========================================================

    print()
    print("========================================")
    print("              RECORDING")
    print("========================================")

    print(f"Packets received : {packet_count}")
    print(f"Samples received : {sample_count}")

    if sample_count > 1:

        total_time = timestamps[-1] - timestamps[0]

        measured_rate = (
            (sample_count - 1)
            / total_time
        )

        print(
            f"Duration         : {total_time:.3f} s"
        )

        print(
            f"Measured rate    : {measured_rate:.2f} Hz"
        )


    # ========================================================
    # DT STATISTICS
    # ========================================================

    if dt_data:

        dt_us = [
            dt * 1_000_000.0
            for dt in dt_data
        ]

        avg_dt = sum(dt_us) / len(dt_us)

        print()
        print("Timestamp timing:")
        print(
            f"Average dt       : {avg_dt:.2f} us"
        )

        print(
            f"Minimum dt       : {min(dt_us):.2f} us"
        )

        print(
            f"Maximum dt       : {max(dt_us):.2f} us"
        )

        bad_intervals = [
            dt for dt in dt_us
            if abs(dt - EXPECTED_DT_US) > 500
        ]

        print(
            f"Intervals > ±500 us : "
            f"{len(bad_intervals)}"
        )


    # ========================================================
    # PLOT
    # ========================================================

    if not timestamps:

        print()
        print("No samples received.")
        return

    print()
    print("Opening plots...")


    # ========================================================
    # ACCELERATION PLOT
    # ========================================================

    plt.figure(figsize=(12, 6))

    plt.plot(
        timestamps,
        ax_data,
        label="AX"
    )

    plt.plot(
        timestamps,
        ay_data,
        label="AY"
    )

    plt.plot(
        timestamps,
        az_data,
        label="AZ"
    )

    plt.xlabel("Time (s)")
    plt.ylabel("Acceleration (g)")

    plt.title(
        "IMU Acceleration"
    )

    plt.legend()
    plt.grid(True)

    plt.tight_layout()


    # ========================================================
    # SAMPLE INTERVAL PLOT
    # ========================================================

    if dt_data:

        dt_time = timestamps[1:]

        dt_us = [
            dt * 1_000_000.0
            for dt in dt_data
        ]

        plt.figure(figsize=(12, 5))

        plt.plot(
            dt_time,
            dt_us,
            label="Measured dt"
        )

        plt.axhline(
            EXPECTED_DT_US,
            linestyle="--",
            label="Expected 416 Hz"
        )

        plt.xlabel("Time (s)")
        plt.ylabel("Sample interval (µs)")

        plt.title(
            "IMU Sample Timing"
        )

        plt.legend()
        plt.grid(True)

        plt.tight_layout()


    # ========================================================
    # SHOW
    # ========================================================

    plt.show()


# ============================================================
# RUN
# ============================================================

if __name__ == "__main__":

    asyncio.run(main())