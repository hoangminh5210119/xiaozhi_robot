1. Lý do chọn làm dự án này

Hiện nay, dân số Việt Nam và thế giới đang bước vào giai đoạn già hóa nhanh chóng. Nhiều người cao tuổi sống một mình hoặc có ít người chăm sóc, dẫn đến nguy cơ quên uống thuốc, khó liên lạc với người thân, hoặc không được hỗ trợ kịp thời khi gặp sự cố sức khỏe.

Dự án thiết bị hỗ trợ người già bằng giọng nói ra đời nhằm mục tiêu:

Giúp người lớn tuổi dễ dàng giao tiếp và điều khiển thiết bị bằng giọng nói, không cần thao tác phức tạp.

Theo dõi sức khỏe (như nhịp tim) và gửi thông tin tự động cho người thân qua Telegram.

Nhắc nhở uống thuốc, lưu vị trí đồ vật, và hỗ trợ người già sinh hoạt hằng ngày.

Tăng cường sự gắn kết giữa người cao tuổi và gia đình thông qua hệ thống cảnh báo, nhắn tin và gọi thoại.

2. Cấu tạo thiết bị 

- ESP32 S3 N16R8 WiFi + Bluetooth
- Màn hình LCD 2.4 inch Driver ST7789
- Microphone I2S INMP441 
- MAX98357A I2S Amplifier
- Module nhịp tim BlackBird bluetooth
- Nút bấm, loa.
- Pin sạc 18650 + mạch sạc 2A
- Mạch nâng áp 5V cho thiết bị
- Động cơ bước (step 42 nema 17) điều khiển xe.
- driver điều khiển động cơ bước A4988.
- Gá đỡ động cơ.
- Servo SG90 điều khiển cửa tủ đựng đồ.
- Khung vỏ in 3D.

1. Tính năng của thiết bị 

- Nhận lệnh giọng nói và phản hồi bằng giọng nói.
- Hotword "Alexa" để kích hoạt thiết bị.
- Theo dõi nhịp tim và gửi cảnh báo qua Telegram.
- Nhắc nhở uống thuốc theo lịch.
- Lưu vị trí đồ vật bằng giọng nói.
- Nhắn tin và nhận tin nhắn qua Telegram.
- Điều khiển xe mini bằng giọng nói.
- Trò chuyện đơn giản với người dùng.
- Xử lý và nhận diện hình ảnh (đọc chữ, nhận diện đồ vật, tiền)
- Gửi hình ảnh qua Telegram.

1. Nguyên lý hoạt động của thiết bị
  

- Thiết bị sử dụng vi điều khiển ESP32 S3 có khả năng xử lý giọng nói thông qua server tích hợp AI xử lý ngôn ngữ tự nhiên để nhận lệnh từ người dùng.
- Thiết bị có màn hình LCD để hiển thị thông tin, trạng thái và phản hồi từ người dùng.
- Microphone I2S INMP441 thu âm giọng nói của người dùng và truyền dữ liệu âm thanh đến ESP32 S3 để xử lý.
- Module nhịp tim BlackBird bluetooth kết nối với thiết bị để đo nhịp tim và gửi dữ liệu về ESP32 S3.
- Thiết bị sử dụng loa để phát âm thanh phản hồi và nhắc nhở người dùng.
- Thiết bị có khả năng kết nối WiFi để gửi thông tin nhanh chóng đến người thân qua Telegram.
- Động cơ bước và driver A4988 giúp thiết bị di chuyển linh hoạt, hỗ trợ người già trong việc lấy đồ vật hoặc di chuyển trong nhà.
- Pin sạc 18650 và mạch sạc 2A cung cấp nguồn điện ổn định cho thiết bị hoạt động liên tục.
- Mạch nâng áp 5V đảm bảo thiết bị nhận đủ điện áp cần thiết để hoạt động hiệu quả.
- Khung vỏ in 3D giúp bảo vệ các linh kiện bên trong và tạo dáng vẻ thẩm mỹ cho thiết bị.

1. Một số hình ảnh của thiết bị đang hoàn thiện.

