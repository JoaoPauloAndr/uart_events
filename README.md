# Comunicação com ESP32 via UART

Este código usa o exemplo da ESP-IDF `uart_events` como base. A partir dele, foi feito o processamento de mensagens no padrão de comandos AT recebidas via UART.

## Como Usar

### Hardware Requerido

Este exemplo foi feito para e testado em placas ESP32-WROOM-32 e ESP32-C3.

### Configurar o projeto

```
idf.py menuconfig
```

### Build e Flash

```
idf.py build
idf.py -p PORT flash monitor
```

(Toggle do monitor serial: ``Ctrl-]``)