# Gale

Gale is an open hardware and software project for controlling a common 3-speed fan with heart rate readings from a Bluetooth HR monitor.

## TODO

 - Pulse the built-in LED when the fan is on
 - Implement a simple Web UI to configure and control the fan (here's a good example of how to achieve that https://lastminuteengineers.com/creating-esp32-web-server-arduino-ide/)
 - Implement controlling the fan with a BT speed sensor
 - FIX: "lld_pdu_get_tx_flush_nb HCI packet count mismatch (0, 1)" that happens somewhere after "Created client" and before "Connected to server"
