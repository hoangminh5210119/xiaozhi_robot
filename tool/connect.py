# import asyncio
# from bleak import BleakScanner, BleakClient

# # Characteristic ví dụ: Battery Level (thường có UUID: 00002a19-0000-1000-8000-00805f9b34fb).
# # Thực tế bạn phải đổi UUID này thành UUID characteristic mà bạn cần đọc.
# # BATTERY_CHAR_UUID = "76A9AF52-CF79-F2FD-52E9-40B32B76EEA8"
# import asyncio
# from bleak import BleakClient

# BATTERY_CHAR_UUID = "00002a19-0000-1000-8000-00805f9b34fb"
# HEART_RATE_MEAS_CHAR_UUID = "00002a37-0000-1000-8000-00805f9b34fb"

# # Callback xử lý dữ liệu heart rate notify
# def heart_rate_handler(sender, data):
#     # data[0] là flags
#     # data[1] là giá trị nhịp tim (nếu là 8-bit)
#     # Nếu là 16-bit, ta cần tách data[1] và data[2]
#     # Thí dụ cơ bản dưới đây cho trường hợp 8-bit:
#     flags = data[0]
#     hr_value = data[1]
#     print(f"Heart Rate Measurement: {hr_value} bpm (Flags={flags})")

# async def main(device_address):
#     async with BleakClient(device_address) as client:
#         connected = await client.is_connected()
#         if connected:
#             print("Đã kết nối tới thiết bị BLE.")

#             # Đọc Battery Level (2A19)
#             try:
#                 battery_data = await client.read_gatt_char(BATTERY_CHAR_UUID)
#                 print(f"Mức Pin: {battery_data[0]}%")
#             except Exception as e:
#                 print("Không đọc được Battery Level:", e)

#             # Đăng ký notify cho Heart Rate Measurement (2A37)
#             try:
#                 await client.start_notify(HEART_RATE_MEAS_CHAR_UUID, heart_rate_handler)
#                 print("Đã bật notify cho Heart Rate Measurement.")
#             except Exception as e:
#                 print("Không subscribe được Heart Rate Measurement:", e)

#             # Chờ 10 giây để minh họa
#             await asyncio.sleep(100)

#             # Tắt notify
#             # await client.stop_notify(HEART_RATE_MEAS_CHAR_UUID)
#             # print("Đã tắt notify Heart Rate Measurement.")
#         else:
#             print("Không thể kết nối tới thiết bị.")

# if __name__ == "__main__":
#     # Thay thế "xx:xx:xx:xx:xx:xx" bằng địa chỉ thật của thiết bị
#     # device_address = "76A9AF52-CF79-F2FD-52E9-40B32B76EEA8"
#     # device_address = "8E3CF592-B655-8898-9B08-5915AEBD3A4E"
#     device_address = "B775379A-01CA-D155-79FD-591ED1AE8EAC"
#     asyncio.run(main(device_address))



import asyncio
from bleak import BleakClient

BATTERY_CHAR_UUID = "00002a19-0000-1000-8000-00805f9b34fb"
HEART_RATE_MEAS_CHAR_UUID = "00002a37-0000-1000-8000-00805f9b34fb"

def heart_rate_handler(sender, data):
    flags = data[0]
    hr_value = data[1]
    print(f"Heart Rate Measurement: {hr_value} bpm (Flags={flags})")

async def main(device_address):
    async with BleakClient(device_address) as client:
        connected = client.is_connected  # ✅ không còn await và ()
        if connected:
            print("Đã kết nối tới thiết bị BLE.")

            # Đọc Battery Level
            try:
                battery_data = await client.read_gatt_char(BATTERY_CHAR_UUID)
                print(f"Mức Pin: {battery_data[0]}%")
            except Exception as e:
                print("Không đọc được Battery Level:", e)

            # Đăng ký notify
            try:
                await client.start_notify(HEART_RATE_MEAS_CHAR_UUID, heart_rate_handler)
                print("Đã bật notify cho Heart Rate Measurement.")
            except Exception as e:
                print("Không subscribe được Heart Rate Measurement:", e)

            await asyncio.sleep(100)
        else:
            print("Không thể kết nối tới thiết bị.")

if __name__ == "__main__":
    device_address = "B775379A-01CA-D155-79FD-591ED1AE8EAC"
    asyncio.run(main(device_address))
