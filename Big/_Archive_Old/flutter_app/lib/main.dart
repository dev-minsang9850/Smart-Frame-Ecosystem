import 'package:flutter/material.dart';
import 'package:shared_preferences/shared_preferences.dart';
import 'ble_screen.dart';
import 'gallery_screen.dart';

void main() async {
  WidgetsFlutterBinding.ensureInitialized();
  
  // Check if we already have the saved ESP32 IP address
  final prefs = await SharedPreferences.getInstance();
  final savedIp = prefs.getString('esp32_ip') ?? '';

  runApp(SmartFrameApp(initialIp: savedIp));
}

class SmartFrameApp extends StatelessWidget {
  final String initialIp;

  const SmartFrameApp({super.key, required this.initialIp});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Smart Frame Manager',
      theme: ThemeData(
        useMaterial3: true,
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.deepPurple,
          brightness: Brightness.dark,
        ),
        appBarTheme: const AppBarTheme(
          centerTitle: true,
          elevation: 2,
        ),
      ),
      home: initialIp.isNotEmpty 
          ? GalleryScreen(esp32Ip: initialIp)
          : const BleScreen(),
      routes: {
        '/ble': (context) => const BleScreen(),
      },
    );
  }
}
