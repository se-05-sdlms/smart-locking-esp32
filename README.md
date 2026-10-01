# Smart Locking ESP32 Firmware (SE-05 SDLMS)

Mã nguồn Firmware điều khiển phần cứng Smart Locker sử dụng vi điều khiển **ESP32** thuộc hệ thống Quản lý Giao nhận Tủ Locker Thông minh (**SE-05 SDLMS**).

Firmware áp dụng kiến trúc **Plug & Play (Cắm là chạy)** chuẩn IoT công nghiệp: **Chỉ sử dụng 1 file mã nguồn duy nhất nạp cho tất cả các tủ** (Tủ 1 ngăn, Tủ 4 ngăn, Tủ A, Tủ B, hoặc bất kỳ tủ mới nào phát sinh sau này mà không cần sửa code).

---

## 1. Cấu trúc thư mục

```text
smart-locking-esp32/
├── firmware/
│   ├── firmware.ino          # Firmware Arduino chính (xử lý đa nhiệm, tự nhận diện MAC)
│   └── secrets.h             # File cấu hình WiFi & MQTT cục bộ (được .gitignore bảo vệ, không đẩy lên Git)
│
├── .gitignore                # Chặn hoàn toàn secrets.h và file nhạy cảm
└── README.md                 # Tài liệu kỹ thuật
```

---

## 2. Cơ chế Tự Nhận Diện Thiết Bị (Plug & Play)

Mỗi con chip ESP32 được nhà sản xuất khắc sẵn một địa chỉ MAC phần cứng duy nhất (eFuse MAC). Khi khởi động:
1. Firmware tự động giải mã 6 ký tự cuối của MAC để tạo mã **Device Identifier**:
   $$\text{eFuse MAC: } \texttt{24:6F:28:8C:3B:12} \implies \text{Device ID: } \mathbf{LKR\text{-}8C3B12}$$
2. ESP32 tự động đăng ký (subscribe) lắng nghe **Topic MQTT chuẩn duy nhất**:
   * `lockers/LKR-8C3B12/doors/+` (Ví dụ mở ngăn 1: `lockers/LKR-8C3B12/doors/1`)
3. Màn hình Serial Monitor sẽ in ra đầy đủ thông tin:
   ```text
   ==========================================================
      SMART LOCKING SYSTEM (SE-05 SDLMS) - ESP32 FIRMWARE    
   ==========================================================
      Hardware Chip MAC : 24:6F:28:XX:XX:XX
      DEVICE IDENTIFIER : LKR-XXXXXX
      WiFi Network      : <Ten_WiFi_Ket_Noi>
      IP Address        : 192.168.1.x
      MQTT Broker       : your_mqtt_broker.emqxsl.com:8883
      Status            : ONLINE & READY
   ==========================================================
      HUONG DAN CHO ADMIN WEB:
      -> Copy Device ID [LKR-8C3B12] va dan vao trang Web Admin.
   ==========================================================
   ```

---

## 3. Quy trình thêm Tủ Mới (Không Cần Sửa Code)

Khi nhóm lắp đặt thêm bất kỳ tủ mới nào:
1. **Nạp code:** Cắm cáp nạp file `firmware/firmware.ino` vào bo mạch ESP32 mới (không sửa bất kỳ dòng code nào).
2. **Xem mã:** Mở Serial Monitor để lấy mã `DEVICE IDENTIFIER` vừa hiển thị (ví dụ: `LKR-8C3B12`).
3. **Cấu hình trên Web Admin:**
   * Đăng nhập Web Admin $\rightarrow$ Vào mục **Quản lý Tủ (Locker Management)** $\rightarrow$ Bấm **Thêm tủ mới**.
   * Nhập **Mã thiết bị (Device Identifier)**: Dán mã `LKR-8C3B12`.
   * Đặt tên tủ (ví dụ: *"Tủ Sảnh A Tòa FPT"*), địa chỉ, và số ngăn tủ mong muốn (1 ngăn, 2 ngăn, 4 ngăn,...).
   $\implies$ **Tủ mới hoạt động ngay lập tức!**

---

## 4. Sơ đồ Chân Phần Cứng (GPIO Pinout)

Firmware hỗ trợ điều khiển tối đa 4 ngăn tủ vật lý (kèm cảm biến công tắc hành trình báo trạng thái đóng/mở cửa) trên cùng 1 mạch ESP32:

### 4.1. Bảng Đấu Nối Chi Tiết

| Ngăn Tủ | Kênh Kỹ Thuật | Module Relay (Kích Khóa) | Công Tắc Hành Trình (Báo Cửa) | Topic Điều Khiển (Sub) | Topic Trạng Thái Cửa (Pub) |
| :---: | :---: | :---: | :---: | :---: | :---: |
| **Ngăn 1** | Kênh 1 | **IN1 $\leftrightarrow$ GPIO 23** | **NO $\leftrightarrow$ GPIO 32** (C $\rightarrow$ GND) | `lockers/{id}/doors/1` | `lockers/{id}/doors/1/status` |
| **Ngăn 2** | Kênh 2 | **IN2 $\leftrightarrow$ GPIO 22** | **NO $\leftrightarrow$ GPIO 33** (C $\rightarrow$ GND) | `lockers/{id}/doors/2` | `lockers/{id}/doors/2/status` |
| **Ngăn 3** | Kênh 3 | **IN3 $\leftrightarrow$ GPIO 21** | **NO $\leftrightarrow$ GPIO 25** (C $\rightarrow$ GND) | `lockers/{id}/doors/3` | `lockers/{id}/doors/3/status` |
| **Ngăn 4** | Kênh 4 | **IN4 $\leftrightarrow$ GPIO 19** | **NO $\leftrightarrow$ GPIO 26** (C $\rightarrow$ GND) | `lockers/{id}/doors/4` | `lockers/{id}/doors/4/status` |

### 4.2. Nguyên lý hoạt động Công Tắc Hành Trình (Door Sensor)
* **Chế độ chân:** ESP32 sử dụng điện trở kéo lên nội `INPUT_PULLUP` (Không cần gắn thêm điện trở ngoài).
* **Đấu dây:** Chân `C` (Common) nối chung vào `GND` của ESP32. Chân `NO` (Normally Open) nối vào chân GPIO tương ứng.
* **Logic tín hiệu:**
  * **Cửa ĐÓNG chặt:** Cánh cửa ép cần gạt công tắc $\rightarrow$ Chân NO chạm chân C (GND) $\rightarrow$ GPIO đọc `LOW` $\implies$ Tự động gửi MQTT payload `"CLOSED"` (Retain).
  * **Cửa MỞ bung ra:** Cần gạt nảy lên $\rightarrow$ Hở mạch $\rightarrow$ GPIO được kéo lên 3.3V đọc `HIGH` $\implies$ Tự động gửi MQTT payload `"OPEN"` (Retain).
* **Chống dội phím (Debounce):** Firmware tích hợp sẵn thuật toán debounce `50ms` chống rung cơ học khi sập cửa.

* **Đối với tủ 1 ngăn (như Tủ B):** Dùng Relay GPIO 23, Cảm biến cửa GPIO 32.
* **Đối với tủ 4 ngăn (như Tủ A):** Dùng đủ 4 cặp Relay và Cảm biến cửa theo bảng trên.

---

## 5. Hướng dẫn Nạp Code Cục Bộ Cho Thành Viên Nhóm

### Bước 1: Clone repo về máy
```bash
git clone https://github.com/se-05-sdlms/smart-locking-esp32.git
```

### Bước 2: Tạo file `secrets.h` (Lưu thông tin bảo mật)
Vì file `secrets.h` chứa thông tin nhạy cảm đã được `.gitignore` chặn lại để chống lộ mật khẩu, mỗi thành viên khi kéo code về cần tạo một file `secrets.h` ngay trong thư mục `firmware/` theo 1 trong 2 cách:

* **Cách 1 (Nhanh nhất - Ngay trên Arduino IDE):**
  1. Mở file `firmware/firmware.ino` bằng **Arduino IDE**.
  2. Bấm vào biểu tượng dấu **`⋮` (3 chấm dọc)** ở góc trên bên phải thanh tab code (hoặc nhấn tổ hợp phím `Ctrl + Shift + N`) $\rightarrow$ Chọn **New Tab**.
  3. Đặt tên tab mới là: **`secrets.h`** rồi bấm **OK**.
* **Cách 2 (Bằng File Explorer / Notepad):**
  Vào thư mục `firmware/`, tạo một file mới đặt tên là **`secrets.h`** (lưu ý xóa đuôi `.txt` nếu có).

### Bước 3: Dán nội dung mẫu vào `secrets.h` và điền thông tin
Dán đoạn mã dưới đây vào file `secrets.h` vừa tạo và điền các thông tin của bạn vào các dấu ngoặc kép `""`:

```cpp
#ifndef SECRETS_H
#define SECRETS_H

// 1. Cấu hình mạng WiFi (Cần mạng 2.4GHz để ESP32 kết nối)
const char* WIFI_SSID     = "";   // Nhập tên WiFi (hoặc điểm phát sóng 4G cá nhân)
const char* WIFI_PASSWORD = "";   // Nhập mật khẩu WiFi

// 2. Cấu hình MQTT Broker (EMQX Cloud Serverless)
const char* MQTT_SERVER   = "";   // Nhập địa chỉ Broker (do nhóm cung cấp)
const int   MQTT_PORT     = 8883; // Cổng kết nối bảo mật MQTTS (TLS/SSL)
const char* MQTT_USER     = "";   // Nhập tài khoản MQTT
const char* MQTT_PASS     = "";   // Nhập mật khẩu MQTT

// 3. Tùy chọn định danh: Để trống "" để chip tự động sinh mã duy nhất theo MAC phần cứng
const char* CUSTOM_DEVICE_ID = "";

#endif
```

### Bước 4: Nạp code vào ESP32
1. Kết nối bo mạch ESP32 với máy tính qua cáp Micro-USB/Type-C có truyền dữ liệu (Data cable).
2. Trên Arduino IDE, chọn:
   * **Board:** `ESP32 Dev Module`
   * **Port:** Chọn đúng cổng `COM` của ESP32.
3. Bấm nút **Upload (Mũi tên sang phải)** để nạp code.
4. Mở **Serial Monitor** (tốc độ `115200 baud`) để xem mã thiết bị `LKR-XXXXXX` được cấp tự động.

---

## 6. Danh sách Thiết bị Phần cứng Thực tế (Lab Hardware Allocation)

Hai bo mạch vi điều khiển ESP32 thực tế của dự án đã được định danh và kiểm thử hoạt động thành công 100%:

| Cụm Tủ | Số Ngăn | Địa Chỉ MAC Phần Cứng | Mã Thiết Bị (**Device Identifier**) | Topic MQTT Lắng Nghe | Sơ Đồ Chân Relay | Trạng Thái |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Tủ A** | 4 ngăn | `20:50:0D:30:92:28` | **`LKR-309228`** | `lockers/LKR-309228/doors/+` | GPIO 23, 22, 21, 19 | Đã kiểm thử thành công |
| **Tủ B** | 1 ngăn | `20:50:0D:1A:08:44` | **`LKR-1A0844`** | `lockers/LKR-1A0844/doors/+` | GPIO 23 | Đã kiểm thử thành công |

> [!IMPORTANT]
> **Hướng dẫn cấu hình trên Web Admin:**
> Khi Quản trị viên (Admin) đăng nhập vào Web để tạo mới tủ locker, hãy nhập chính xác mã:
> * Tủ A: Điền ô *Device Identifier* là **`LKR-309228`**
> * Tủ B: Điền ô *Device Identifier* là **`LKR-1A0844`**

