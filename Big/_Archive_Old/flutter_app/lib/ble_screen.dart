import 'dart:async';
import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'wifi_screen.dart';
import 'gallery_screen.dart';

class BleScreen extends StatefulWidget {
  const BleScreen({super.key});

  @override
  State<BleScreen> createState() => _BleScreenState();
}

class _BleScreenState extends State<BleScreen> {
  List<ScanResult> _scanResults = [];
  bool _isScanning = false;
  late StreamSubscription<List<ScanResult>> _scanResultsSubscription;
  late StreamSubscription<bool> _isScanningSubscription;

  @override
  void initState() {
    super.initState();
    _requestPermissions();

    _scanResultsSubscription = FlutterBluePlus.scanResults.listen((results) {
      // Filter results to avoid duplicates and show only relevant devices if preferred,
      // here we sort or display devices having a name.
      setState(() {
        _scanResults = results.where((r) => r.device.platformName.isNotEmpty).toList();
      });
    });

    _isScanningSubscription = FlutterBluePlus.isScanning.listen((state) {
      setState(() {
        _isScanning = state;
      });
    });
  }

  Future<void> _requestPermissions() async {
    // Request Bluetooth and location permissions
    Map<Permission, PermissionStatus> statuses = await [
      Permission.bluetoothScan,
      Permission.bluetoothConnect,
      Permission.location,
    ].request();

    if (statuses[Permission.bluetoothScan] != PermissionStatus.granted ||
        statuses[Permission.bluetoothConnect] != PermissionStatus.granted) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Bluetooth permissions are required to scan for the frame.')),
        );
      }
    }
  }

  void _startScan() async {
    if (await FlutterBluePlus.isSupported == false) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(content: Text('Bluetooth is not supported on this device.')),
        );
      }
      return;
    }

    try {
      _scanResults.clear();
      await FlutterBluePlus.startScan(timeout: const Duration(seconds: 5));
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Error starting scan: $e')),
        );
      }
    }
  }

  void _stopScan() async {
    try {
      await FlutterBluePlus.stopScan();
    } catch (e) {
      debugPrint('Error stopping scan: $e');
    }
  }

  void _connectToDevice(BluetoothDevice device) async {
    _stopScan();
    
    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (context) => const Center(
        child: Card(
          child: Padding(
            padding: EdgeInsets.all(24.0),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              children: [
                CircularProgressIndicator(),
                SizedBox(height: 16),
                Text('Connecting to Smart Frame...'),
              ],
            ),
          ),
        ),
      ),
    );

    try {
      await device.connect(timeout: const Duration(seconds: 10));
      
      if (mounted) {
        Navigator.pop(context); // Close connecting dialog
        
        // Navigate to Wi-Fi provisioning page
        Navigator.push(
          context,
          MaterialPageRoute(
            builder: (context) => WifiScreen(device: device),
          ),
        );
      }
    } catch (e) {
      if (mounted) {
        Navigator.pop(context); // Close dialog
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Connection failed: $e')),
        );
      }
    }
  }

  @override
  void dispose() {
    _scanResultsSubscription.cancel();
    _isScanningSubscription.cancel();
    super.dispose();
  }

  void _showManualIpDialog(BuildContext context) {
    final TextEditingController ipController = TextEditingController();
    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('직접 IP 입력 (Skip Bluetooth)'),
        content: TextField(
          controller: ipController,
          decoration: const InputDecoration(
            hintText: 'ex) 192.168.0.10',
            border: OutlineInputBorder(),
          ),
          keyboardType: const TextInputType.numberWithOptions(decimal: true),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('취소'),
          ),
          ElevatedButton(
            onPressed: () async {
              final ip = ipController.text.trim();
              if (ip.isNotEmpty) {
                final prefs = await SharedPreferences.getInstance();
                await prefs.setString('esp32_ip', ip);
                if (mounted) {
                  Navigator.pop(context);
                  Navigator.pushReplacement(
                    context,
                    MaterialPageRoute(builder: (context) => GalleryScreen(esp32Ip: ip)),
                  );
                }
              }
            },
            child: const Text('연결'),
          ),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Connect Smart Frame'),
        actions: [
          if (_isScanning)
            IconButton(
              icon: const Icon(Icons.stop),
              onPressed: _stopScan,
            )
          else
            IconButton(
              icon: const Icon(Icons.refresh),
              onPressed: _startScan,
            ),
        ],
      ),
      floatingActionButton: FloatingActionButton.extended(
        onPressed: () => _showManualIpDialog(context),
        icon: const Icon(Icons.wifi),
        label: const Text('직접 IP 입력'),
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
        child: Column(
          children: [
            if (_isScanning)
              const LinearProgressIndicator(color: Colors.deepPurpleAccent),
            Expanded(
              child: _scanResults.isEmpty
                  ? Center(
                      child: Column(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          const Icon(Icons.bluetooth_searching, size: 64, color: Colors.grey),
                          const SizedBox(height: 16),
                          const Text(
                            'No Smart Frames found nearby.',
                            style: TextStyle(fontSize: 16, color: Colors.grey),
                          ),
                          const SizedBox(height: 24),
                          ElevatedButton.icon(
                            onPressed: _startScan,
                            icon: const Icon(Icons.search),
                            label: const Text('Scan for Devices'),
                            style: ElevatedButton.styleFrom(
                              padding: const EdgeInsets.symmetric(horizontal: 24, vertical: 12),
                            ),
                          ),
                        ],
                      ),
                    )
                  : ListView.builder(
                      itemCount: _scanResults.length,
                      itemBuilder: (context, index) {
                        final result = _scanResults[index];
                        final device = result.device;
                        final name = device.platformName;
                        final isSmartFrame = name == 'ESP32-SmartFrame';

                        return Card(
                          margin: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                          shape: RoundedRectangleBorder(
                            borderRadius: BorderRadius.circular(12),
                            side: BorderSide(
                              color: isSmartFrame ? Colors.deepPurpleAccent : Colors.transparent,
                              width: 1.5,
                            ),
                          ),
                          child: ListTile(
                            leading: CircleAvatar(
                              backgroundColor: isSmartFrame ? Colors.deepPurple : Colors.grey[800],
                              child: const Icon(Icons.photo_album, color: Colors.white),
                            ),
                            title: Text(
                              name,
                              style: TextStyle(
                                fontWeight: isSmartFrame ? FontWeight.bold : FontWeight.normal,
                              ),
                            ),
                            subtitle: Text(device.remoteId.toString()),
                            trailing: ElevatedButton(
                              onPressed: () => _connectToDevice(device),
                              style: ElevatedButton.styleFrom(
                                backgroundColor: isSmartFrame ? Colors.deepPurpleAccent : null,
                                foregroundColor: isSmartFrame ? Colors.white : null,
                              ),
                              child: const Text('Connect'),
                            ),
                          ),
                        );
                      },
                    ),
            ),
          ],
        ),
      ),
    );
  }
}
