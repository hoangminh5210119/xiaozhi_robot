import asyncio
from bleak import BleakScanner, BleakClient

def guid_to_mac(guid: str) -> str:
    # Loại bỏ dấu gạch ngang
    hex_str = guid.replace('-', '')
    # Chuyển GUID thành số nguyên 128-bit
    num = int(hex_str, 16)
    # Lấy 48 bit thấp nhất
    mac_int = num & 0xFFFFFFFFFFFF
    # Chuyển về chuỗi MAC theo định dạng aa:bb:cc:dd:ee:ff
    mac = ':'.join(f"{(mac_int >> i) & 0xff:02x}" for i in range(40, -1, -8))
    return mac

async def run():
    # Quét các thiết bị BLE trong 5 giây
    print("Đang quét các thiết bị BLE...")
    devices = await BleakScanner.discover(timeout=5.0)

    if not devices:
        print("Không tìm thấy thiết bị nào.")
        return

    # Hiển thị danh sách các thiết bị tìm được cùng với MAC address chuyển đổi
    for i, device in enumerate(devices):
        converted_mac = guid_to_mac(device.address)
        print(f"{i}: {device.name} [{device.address}] -> MAC: {converted_mac}")

    # Chọn thiết bị đầu tiên làm ví dụ (có thể thay đổi theo nhu cầu)
    selected_device = devices[0]
    print(f"\nKết nối đến thiết bị: {selected_device.name} [{selected_device.address}]")

    async with BleakClient(selected_device) as client:
        if client.is_connected:
            print("Kết nối thành công!")
        else:
            print("Kết nối thất bại!")

if __name__ == "__main__":
    asyncio.run(run())
