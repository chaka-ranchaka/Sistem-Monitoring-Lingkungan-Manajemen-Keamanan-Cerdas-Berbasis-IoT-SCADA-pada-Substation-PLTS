# Panduan Menjalankan Proyek di Wokwi

Folder ini berisi proyek Wokwi lengkap. Semua file tinggal disalin ke editor wokwi.com.

| File | Isi |
|---|---|
| `PLTS/PLTS.ino` | Firmware ESP32: baca 5 sensor, aturan R1–R11, MQTT (sensor/status/alarm/ack), perintah dari HMI |
| `diagram.json` | Rangkaian: ESP32, 5 sensor, 4 relay + LED beban, servo, buzzer |
| `libraries.txt` | Library yang diunduh otomatis oleh Wokwi (PubSubClient, ArduinoJson) |
| `lm35.chip.json` + `lm35.chip.c` | Custom chip LM35 (slider suhu → 10 mV/°C) |
| `bh1750.chip.json` + `bh1750.chip.c` | Custom chip BH1750 I²C 0x23 (slider lux) |
| `sct013.chip.json` + `sct013.chip.c` | Custom chip SCT-013-000 + burden 22 Ω + bias 1,65 V (slider arus, gelombang 50 Hz) |
| `reservoir.chip.json` + `reservoir.chip.c` | Custom chip HC-SR04 + model tangki air 100 cm (membalas TRIG dengan pulsa ECHO) |
| `pir.chip.json` + `pir.chip.c` | Custom chip PIR HC-SR501 (keluaran digital HIGH saat ada gerakan) |

## Komponen yang tidak ada di Wokwi

| Komponen | Di Wokwi | Cara di proyek ini |
|---|---|---|
| ESP32, servo SG90, buzzer | Ada | Komponen bawaan |
| HC-SR04, PIR HC-SR501 | Ada, tetapi nilainya tidak bisa diubah otomatis | Diganti custom chip agar bisa berjalan dalam mode skenario |
| Relay 4 kanal | Hanya relay 1 kanal | 4 × `wokwi-relay-module`, beban ditunjukkan LED |
| LM35 | Tidak ada | Custom chip |
| BH1750 | Tidak ada | Custom chip (protokol I²C asli) |
| SCT-013 | Tidak ada | Custom chip 2 terminal (GND, OUT) seperti jack SCT-013; burden 22 Ω dan bias 1,65 V dimodelkan di dalam chip |
| Resistor pembagi ECHO, burden, transistor buzzer | Wokwi tidak menyimulasikan rangkaian analog | Tidak dipasang di simulasi, tetap digambar di wiring diagram Fritzing |

Custom chip adalah fitur resmi Wokwi: komponen buatan sendiri yang ditulis dalam C, lengkap dengan slider untuk mengubah nilai saat simulasi.
Itu sebabnya hasilnya lebih dekat ke sensor asli daripada sekadar mengganti dengan potensiometer.

## Langkah-langkah

1. **Buat proyek.** Login di https://wokwi.com → **New Project** → pilih **ESP32** (template Arduino).
   Proyek baru berisi `sketch.ino` dan `diagram.json`.
2. **Tambahkan file custom chip dulu** (sebelum mengganti `diagram.json`, supaya bagian `chip-…` dikenali).
   Klik panah **▾** di sebelah tab file → **New file…** → ketik nama persis `lm35.chip.json` → tempel isinya.
   Ulangi untuk `lm35.chip.c`, `bh1750.chip.json`, `bh1750.chip.c`, `sct013.chip.json`, `sct013.chip.c`,
   `reservoir.chip.json`, `reservoir.chip.c`, `pir.chip.json`, `pir.chip.c`.
   Nama file harus persis, karena Wokwi menghubungkan bagian `chip-lm35` di diagram ke file `lm35.chip.*`.
3. **Tambahkan `libraries.txt`** dengan cara yang sama (New file…), isi dua baris: `PubSubClient` dan `ArduinoJson`.
4. **Ganti `diagram.json`**: buka tab diagram.json, hapus semua isinya, tempel isi `diagram.json` dari folder ini.
   Semua komponen dan kabel langsung muncul. Kabel tergambar lurus, jadi boleh digeser agar rapi untuk screenshot laporan.
5. **Ganti `sketch.ino`** di Wokwi dengan isi `PLTS/PLTS.ino` dari folder ini.
   Ubah `st01` pada baris `#define TOPIC_PREFIX` dengan kode unikmu (misalnya NIM), karena broker HiveMQ dipakai publik.
6. **Simpan** (Ctrl+S) lalu klik **▶ Start**. Kompilasi pertama memakan waktu lebih lama karena custom chip dan library ikut dikompilasi.
7. **Cek Serial Monitor**: harus muncul `Wi-Fi connect... OK`, `MQTT connect... OK`, lalu baris JSON sensor tiap 2 detik.
   LED biru onboard (GPIO2) menyala jika Wi-Fi dan MQTT tersambung.

## Mode skenario (data bervariasi otomatis)

Kelima chip sensor punya slider **Mode skenario**. Di `diagram.json` semuanya diset **1 (otomatis)**, sehingga nilai sensor berubah sendiri selama simulasi:

| Sensor | Pola skenario (waktu simulasi) | Aturan yang terpicu |
|---|---|---|
| LM35 | Suhu 23–43 °C, siklus 4 menit + fluktuasi; lonjakan ±50 °C tiap 6 menit (mulai detik ke-280) | R1–R3 kipas bertingkat, R4 OVERHEAT |
| Reservoir (HC-SR04) | Level air turun 85 → 15 % selama 100 s, lalu naik ke 85 % selama 50 s (siklus 150 s) | R8 pompa ON + LEVEL_RENDAH, R9 pompa OFF |
| PIR | Orang lewat tiap 20–60 s, sinyal HIGH 4–8 s | R5 lampu, R6 INTRUSI saat ARMED |
| BH1750 | Kurva siang 0 → ±1100 → 0 lux (siklus 4 menit) + awan acak | R10 monitoring |
| SCT-013 | Arus 0,3–1,2 A (siklus 2 menit); lonjakan 2,6 A tiap 6 menit (detik ke-160–180) | R11 ARUS_LEBIH |

Simulasi di VS Code berjalan sekitar 23 % kecepatan normal. Jadi 1 menit waktu simulasi ≈ 4–5 menit waktu nyata, dan **semua kejadian di atas muncul dalam ±25 menit waktu nyata**. Pengambilan data:

1. Pastikan mode **AUTO** di dashboard Node-RED (saklar "Mode MANUAL" mati).
2. Biarkan simulasi berjalan ±25–30 menit. Node-RED otomatis mencatat ke `05_Data/data_sensor_….csv`.
3. Agar alarm intrusi tidak terus berbunyi, matikan saklar **Keamanan ARMED** jika hanya ingin mengamati lampu (R5).

Untuk uji manual per aturan (tabel di bawah), geser slider **Mode skenario** chip yang diuji ke **0**, lalu pakai slider nilainya.

## Cara menguji saat simulasi berjalan

Klik komponen untuk memunculkan slider atau tombolnya.

| Uji | Cara | Hasil yang diharapkan |
|---|---|---|
| R1/R2 | Slider LM35 ke 36 °C, lalu 31 °C | LED Kipas menyala, lalu padam (tetap menyala di 33–34 °C karena histeresis) |
| R3 | LM35 ke 41 °C | LED Kipas 2 ikut menyala |
| R4 | LM35 ke 46 °C | Buzzer berbunyi, alarm `OVERHEAT` muncul di Serial Monitor |
| R5/R6 | Slider "Gerakan" chip PIR ke 1 (lalu kembali ke 0) | LED Lampu menyala 30 s; karena awalnya ARMED, buzzer + alarm `INTRUSI` |
| R8/R9 | Slider "Jarak" chip reservoir ke 90 cm (level 10 %), lalu 15 cm (level 85 %) | LED Pompa menyala, lalu padam |
| R10 | Slider BH1750 | Nilai `lux` di JSON ikut berubah |
| R11 | Slider SCT-013 ke 3 A | Alarm `ARUS_LEBIH` |

Untuk memantau dan mengirim perintah tanpa Node-RED, buka https://www.hivemq.com/demos/websocket-client/
→ Connect → subscribe `pltsiot/2300118/#`. Contoh perintah (publish):

- `pltsiot/2300118/cmd/arm` → `DISARMED`
- `pltsiot/2300118/cmd/mode` → `MANUAL`
- `pltsiot/2300118/cmd/relay` → `{"ch":1,"state":1,"id":1}`
- `pltsiot/2300118/cmd/door` → `{"action":"OPEN","id":2}` (hanya diterima saat DISARMED)

## Catatan untuk laporan

- **Validasi sensor (BAB III):** nilai slider adalah "nilai sebenarnya", nilai di JSON adalah "nilai terukur" → hitung galat %.
- **ADC:** Wokwi memodelkan ADC ESP32 secara linear dengan referensi 5 V (raw = V ÷ 5 V × 4095; terukur 3300 mV → 2703). Karena itu firmware memakai `SIMULASI_WOKWI 1` (mV = raw × 5000 ÷ 4095). Untuk ESP32 asli ubah ke `0`, sehingga dipakai `analogReadMilliVolts()` yang terkalibrasi eFuse. Hasil uji di Wokwi: slider LM35 30 °C terbaca 30,04 °C, slider SCT-013 0,5 A terbaca 0,504 A.
- **Relay:** relay Wokwi bawaannya aktif HIGH, sedangkan modul relay 4-CH asli aktif LOW. Karena itu keempat relay di `diagram.json` diberi `"transistor": "pnp"` agar aktif LOW, sama seperti hardware dan firmware (`RELAY_ACTIVE_LOW = true`).
- **Tata letak diagram:** ESP32 diputar horizontal. Pin atas ke sensor I²C, ultrasonik, dan buzzer; pin bawah ke sensor analog, relay, PIR, dan servo. Daya memakai simbol `VCC` (catu 5 V) dan `GND` (ground bersama) di dekat tiap komponen, jadi tidak ada kabel daya panjang. Rute kabel dihitung agar tidak melewati komponen dan tidak ada kabel yang bersilangan.
- **LED beban (low-side):** COM relay ke GND, NO ke katoda LED, anoda LED ke VCC. Relay memutus jalur ground LED. Sudah diuji di Wokwi: LED menyala saat relay aktif dan padam saat relay mati.
- **QoS:** library PubSubClient hanya bisa *publish* dengan QoS 0 (subscribe bisa QoS 1). Status dan `online` tetap memakai retain.
- **Keamanan (R6):** alarm intrusi bergantung pada status ARMED/DISARMED, bukan mode AUTO/MANUAL, sama seperti R4 yang selalu aktif.

## Menjalankan di Wokwi for VS Code (tanpa antre server)

Wokwi for VS Code menjalankan simulasi di laptop sendiri. Firmware dan custom chip harus sudah dikompilasi; keduanya sudah tersedia di folder ini:

| File | Isi |
|---|---|
| `wokwi.toml` | Konfigurasi: firmware `PLTS/build/PLTS.ino.merged.bin` + 5 custom chip `.wasm` |
| `PLTS/build/` | Hasil kompilasi firmware (ESP32 core 3.3.11) |
| `lm35`, `bh1750`, `sct013`, `reservoir`, `pir` `.chip.wasm` | Custom chip hasil kompilasi `wokwi-cli` |
| `build.ps1` | Skrip kompilasi ulang setelah kode diubah |

Langkah:

1. Di VS Code, pasang ekstensi **Wokwi Simulator** (penerbit: Wokwi).
2. Tekan **F1** → **Wokwi: Request a new License** → browser terbuka → login dan setujui. Lisensi gratis untuk penggunaan pribadi/edukasi.
3. **File → Open Folder…** → pilih folder `04_Wokwi` (folder yang berisi `wokwi.toml`).
4. Buka `diagram.json`, lalu tekan **F1** → **Wokwi: Start Simulator**. Biarkan tab simulator tetap terlihat; jika tab tertutup, simulasi berhenti.
5. Wi-Fi `Wokwi-GUEST` dan koneksi MQTT ke internet berjalan lewat gateway bawaan ekstensi.

Jika `PLTS.ino` diubah, firmware harus dikompilasi ulang sebelum Start Simulator:

```powershell
winget install ArduinoSA.CLI          # sekali saja
powershell -ExecutionPolicy Bypass -File build.ps1
```

Custom chip hanya perlu dikompilasi ulang jika file `.chip.c` diubah (`build.ps1 -Chips`, butuh `wokwi-cli`).
