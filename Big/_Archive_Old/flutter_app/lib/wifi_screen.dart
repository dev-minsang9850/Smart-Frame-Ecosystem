import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'gallery_screen.dart';

class WifiScreen extends StatefulWidget {
  final BluetoothDevice device;

  const WifiScreen({super.key, required this.device});

  @override
  State<WifiScreen> createState() => _WifiScreenState();
}

class _WifiScreenState extends State<WifiScreen> {
  final _formKey = GlobalKey<FormState>();
  final _ssidController = TextEditingController();
  final _passwordController = TextEditingController();
  final _ipController = TextEditingController();

  bool _isSending = false;
  bool _sentSuccess = false;

  static const String serviceUuid = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
  static const String characteristicUuid = "beb5483e-36e1-4688-b7f5-ea07361b26a8";

  Future<void> _sendCredentials() async {
    if (!_formKey.currentState!.validate()) return;

    setState(() {
      _isSending = true;
    });

    try {
      // Ensure the device is connected to avoid "device is not connected" errors
      try {
        await widget.device.connect(timeout: const Duration(seconds: 5));
        await Future.delayed(const Duration(milliseconds: 500)); // wait for connection to settle
      } catch (e) {
        debugPrint("Connection check / reconnect bypass: $e");
      }

      // 0. Request larger MTU size to avoid 20-byte payload restriction (GATT_ERROR 133)
      try {
        await widget.device.requestMtu(512);
        // Wait briefly for MTU negotiations to finish
        await Future.delayed(const Duration(milliseconds: 300));
      } catch (e) {
        debugPrint("MTU request failed or unsupported on this OS: $e");
      }

      // 1. Discover services
      List<BluetoothService> services = await widget.device.discoverServices();
      BluetoothCharacteristic? targetChar;

      for (var service in services) {
        if (service.uuid.toString().toLowerCase() == serviceUuid) {
          for (var char in service.characteristics) {
            if (char.uuid.toString().toLowerCase() == characteristicUuid) {
              targetChar = char;
              break;
            }
          }
        }
      }

      if (targetChar == null) {
        throw Exception("Target characteristic not found. Is this the right ESP32 frame?");
      }

      // 2. Format: "SSID;Password"
      String creds = "${_ssidController.text};${_passwordController.text}";
      List<int> bytes = utf8.encode(creds);

      // 3. Write value (with response) and gracefully bypass the disconnection error when ESP32 reboots instantly
      try {
        await targetChar.write(bytes, withoutResponse: false);
      } catch (e) {
        final errorStr = e.toString().toLowerCase();
        if (errorStr.contains("disconnected") || errorStr.contains("code: 6") || errorStr.contains("code 6")) {
          debugPrint("Reboot disconnection bypassed: $e");
        } else {
          rethrow;
        }
      }
      
      setState(() {
        _isSending = false;
        _sentSuccess = true;
      });

      // Disconnect BLE since the ESP32 will reboot
      await widget.device.disconnect();

      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Wi-Fi credentials sent! ESP32 is rebooting.')),
        );
      }
    } catch (e) {
      setState(() {
        _isSending = false;
      });
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Error writing Wi-Fi configuration: $e')),
        );
      }
    }
  }

  Future<void> _saveIpAndProceed() async {
    final ip = _ipController.text.trim();
    if (ip.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Please enter a valid IP address.')),
      );
      return;
    }

    final prefs = await SharedPreferences.getInstance();
    await prefs.setString('esp32_ip', ip);

    if (mounted) {
      Navigator.pushAndRemoveUntil(
        context,
        MaterialPageRoute(builder: (context) => GalleryScreen(esp32Ip: ip)),
        (route) => false, // Remove all previous screens
      );
    }
  }

  @override
  void dispose() {
    _ssidController.dispose();
    _passwordController.dispose();
    _ipController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Wi-Fi Configuration'),
      ),
      body: Container(
        decoration: BoxDecoration(
          gradient: LinearGradient(
            begin: Alignment.topCenter,
            end: Alignment.bottomCenter,
            colors: [
              Theme.of(context).colorScheme.surface,
              Theme.of(context).colorScheme.surfaceContainerHighest,
            ],
          ),
        ),
        child: Padding(
          padding: const EdgeInsets.all(24.0),
          child: SingleChildScrollView(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                if (!_sentSuccess) ...[
                  const Icon(Icons.wifi_lock, size: 64, color: Colors.deepPurpleAccent),
                  const SizedBox(height: 16),
                  const Text(
                    'Configure Smart Frame Wi-Fi',
                    textAlign: TextAlign.center,
                    style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold),
                  ),
                  const SizedBox(height: 8),
                  const Text(
                    'Enter the SSID and password of your home router. The Smart Frame will reboot and connect to this network.',
                    textAlign: TextAlign.center,
                    style: TextStyle(color: Colors.grey),
                  ),
                  const SizedBox(height: 32),
                  Form(
                    key: _formKey,
                    child: Column(
                      children: [
                        TextFormField(
                          controller: _ssidController,
                          decoration: InputDecoration(
                            labelText: 'Wi-Fi SSID',
                            prefixIcon: const Icon(Icons.wifi),
                            border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                          ),
                          validator: (value) {
                            if (value == null || value.isEmpty) {
                              return 'SSID cannot be empty';
                            }
                            return null;
                          },
                        ),
                        const SizedBox(height: 16),
                        TextFormField(
                          controller: _passwordController,
                          obscureText: true,
                          decoration: InputDecoration(
                            labelText: 'Wi-Fi Password',
                            prefixIcon: const Icon(Icons.lock),
                            border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                          ),
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 32),
                  ElevatedButton(
                    onPressed: _isSending ? null : _sendCredentials,
                    style: ElevatedButton.styleFrom(
                      padding: const EdgeInsets.symmetric(vertical: 16),
                      backgroundColor: Colors.deepPurple,
                      foregroundColor: Colors.white,
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                    ),
                    child: _isSending
                        ? const SizedBox(
                            height: 24,
                            width: 24,
                            child: CircularProgressIndicator(color: Colors.white, strokeWidth: 2),
                          )
                        : const Text('Send Wi-Fi Config', style: TextStyle(fontSize: 16)),
                  ),
                ] else ...[
                  const Icon(Icons.check_circle_outline, size: 80, color: Colors.green),
                  const SizedBox(height: 24),
                  const Text(
                    'Credentials Sent Successfully!',
                    textAlign: TextAlign.center,
                    style: TextStyle(fontSize: 22, fontWeight: FontWeight.bold, color: Colors.green),
                  ),
                  const SizedBox(height: 16),
                  const Text(
                    '1. The Smart Frame is rebooting.\n2. Look at the frame display and wait for it to show the Connected IP Address.\n3. Enter that IP address below to access the photo management interface.',
                    style: TextStyle(fontSize: 15, height: 1.5),
                  ),
                  const SizedBox(height: 32),
                  TextField(
                    controller: _ipController,
                    keyboardType: const TextInputType.numberWithOptions(decimal: true),
                    decoration: InputDecoration(
                      labelText: 'Smart Frame IP Address',
                      hintText: 'e.g. 192.168.1.100',
                      prefixIcon: const Icon(Icons.settings_ethernet),
                      border: OutlineInputBorder(borderRadius: BorderRadius.circular(12)),
                    ),
                  ),
                  const SizedBox(height: 24),
                  ElevatedButton(
                    onPressed: _saveIpAndProceed,
                    style: ElevatedButton.styleFrom(
                      padding: const EdgeInsets.symmetric(vertical: 16),
                      backgroundColor: Colors.green[700],
                      foregroundColor: Colors.white,
                      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
                    ),
                    child: const Text('Open Gallery Control', style: TextStyle(fontSize: 16)),
                  ),
                ],
              ],
            ),
          ),
        ),
      ),
    );
  }
}
