import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import 'dart:convert';
import 'package:image_picker/image_picker.dart';
import 'package:image/image.dart' as img;
import 'package:shared_preferences/shared_preferences.dart';

class GalleryScreen extends StatefulWidget {
  final String esp32Ip;

  const GalleryScreen({super.key, required this.esp32Ip});

  @override
  State<GalleryScreen> createState() => _GalleryScreenState();
}

class _GalleryScreenState extends State<GalleryScreen> {
  List<String> _photos = [];
  bool _isLoading = false;
  bool _isUploading = false;
  final ImagePicker _picker = ImagePicker();

  @override
  void initState() {
    super.initState();
    _fetchPhotos();
  }

  Future<void> _fetchPhotos() async {
    setState(() {
      _isLoading = true;
    });

    try {
      final response = await http.get(
        Uri.parse('http://${widget.esp32Ip}/list'),
      ).timeout(const Duration(seconds: 5));

      if (response.statusCode == 200) {
        final List<dynamic> decoded = json.decode(response.body);
        setState(() {
          _photos = decoded.cast<String>();
        });
      } else {
        throw Exception('Failed to load photos');
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Error communicating with Smart Frame: $e')),
        );
      }
    } finally {
      setState(() {
        _isLoading = false;
      });
    }
  }

  Future<void> _pickAndUploadImage() async {
    final XFile? selected = await _picker.pickImage(
      source: ImageSource.gallery,
    );

    if (selected == null) return;

    setState(() {
      _isUploading = true;
    });

    try {
      // 1. Read bytes from selected file
      final bytes = await selected.readAsBytes();

      // 2. Decode the image using 'image' package (run resize off major UI threads if slow)
      // Decode the image
      img.Image? originalImage = img.decodeImage(bytes);
      if (originalImage == null) {
        throw Exception("Failed to decode the chosen image.");
      }

      // 3. Resize & Letterbox to 320x240 (preserving aspect ratio with black margins)
      double scale = 320 / originalImage.width;
      if (240 / originalImage.height < scale) {
        scale = 240 / originalImage.height;
      }

      int scaledWidth = (originalImage.width * scale).round();
      int scaledHeight = (originalImage.height * scale).round();

      img.Image scaledImage = img.copyResize(
        originalImage,
        width: scaledWidth,
        height: scaledHeight,
        interpolation: img.Interpolation.cubic,
      );

      img.Image finalCanvas = img.Image(width: 320, height: 240);
      img.fill(finalCanvas, color: img.ColorRgb8(0, 0, 0));

      int dstX = (320 - scaledWidth) ~/ 2;
      int dstY = (240 - scaledHeight) ~/ 2;

      img.compositeImage(
        finalCanvas,
        scaledImage,
        dstX: dstX,
        dstY: dstY,
      );

      // 4. Re-compress as JPEG
      List<int> compressedJpg = img.encodeJpg(finalCanvas, quality: 85);

      // 5. Send to ESP32 local HTTP server
      final uri = Uri.parse('http://${widget.esp32Ip}/upload');
      final request = http.MultipartRequest('POST', uri);
      
      final timestamp = DateTime.now().millisecondsSinceEpoch;
      final filename = 'photo_$timestamp.jpg';

      request.files.add(
        http.MultipartFile.fromBytes(
          'file',
          compressedJpg,
          filename: filename,
        ),
      );

      final streamedResponse = await request.send().timeout(const Duration(seconds: 20));
      final response = await http.Response.fromStream(streamedResponse);

      if (response.statusCode == 200) {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('Image uploaded successfully!'), backgroundColor: Colors.green),
          );
        }
        _fetchPhotos(); // Refresh list
      } else {
        throw Exception('Server rejected upload: ${response.body}');
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Failed to upload image: $e'), backgroundColor: Colors.redAccent),
        );
      }
    } finally {
      setState(() {
        _isUploading = false;
      });
    }
  }

  Future<void> _deletePhoto(String filename) async {
    // Show confirmation dialog
    final confirm = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('Delete Photo'),
        content: Text('Are you sure you want to delete "$filename" from the Smart Frame?'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('Cancel'),
          ),
          TextButton(
            onPressed: () => Navigator.pop(context, true),
            style: TextButton.styleFrom(foregroundColor: Colors.red),
            child: const Text('Delete'),
          ),
        ],
      ),
    );

    if (confirm != true) return;

    setState(() {
      _isLoading = true;
    });

    try {
      final response = await http.delete(
        Uri.parse('http://${widget.esp32Ip}/delete?file=$filename'),
      ).timeout(const Duration(seconds: 5));

      if (response.statusCode == 200) {
        if (mounted) {
          ScaffoldMessenger.of(context).showSnackBar(
            const SnackBar(content: Text('Photo deleted from frame.')),
          );
        }
        _fetchPhotos(); // Refresh
      } else {
        throw Exception(response.body);
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Error deleting photo: $e'), backgroundColor: Colors.redAccent),
        );
      }
    } finally {
      setState(() {
        _isLoading = false;
      });
    }
  }

  Future<void> _resetConnection() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove('esp32_ip');
    if (mounted) {
      Navigator.pushReplacementNamed(context, '/ble');
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Smart Frame Gallery'),
        actions: [
          IconButton(
            icon: const Icon(Icons.refresh),
            tooltip: 'Refresh Gallery',
            onPressed: _isLoading ? null : _fetchPhotos,
          ),
          IconButton(
            icon: const Icon(Icons.power_settings_new),
            tooltip: 'Change Device Connection',
            onPressed: _resetConnection,
          ),
        ],
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
        child: _isLoading && _photos.isEmpty
            ? const Center(child: CircularProgressIndicator())
            : _photos.isEmpty
                ? Center(
                    child: Column(
                      mainAxisAlignment: MainAxisAlignment.center,
                      children: [
                        const Icon(Icons.photo_library_outlined, size: 64, color: Colors.grey),
                        const SizedBox(height: 16),
                        const Text(
                          'No photos on the Smart Frame yet.',
                          style: TextStyle(fontSize: 16, color: Colors.grey),
                        ),
                        const SizedBox(height: 8),
                        Text(
                          'Frame IP: ${widget.esp32Ip}',
                          style: const TextStyle(fontSize: 12, color: Colors.grey),
                        ),
                      ],
                    ),
                  )
                : RefreshIndicator(
                    onRefresh: _fetchPhotos,
                    child: GridView.builder(
                      padding: const EdgeInsets.all(16),
                      gridDelegate: const SliverGridDelegateWithFixedCrossAxisCount(
                        crossAxisCount: 2,
                        crossAxisSpacing: 16,
                        mainAxisSpacing: 16,
                        childAspectRatio: 4 / 3, // Match aspect ratio 320x240
                      ),
                      itemCount: _photos.length,
                      itemBuilder: (context, index) {
                        final filename = _photos[index];
                        // Get remote image url for preview
                        final imageUrl = 'http://${widget.esp32Ip}/view?file=$filename';

                        return ClipRRect(
                          borderRadius: BorderRadius.circular(12),
                          child: Stack(
                            fit: StackFit.expand,
                            children: [
                              Image.network(
                                imageUrl,
                                fit: BoxFit.cover,
                                errorBuilder: (context, error, stackTrace) {
                                  return Container(
                                    color: Colors.grey[800],
                                    child: const Column(
                                      mainAxisAlignment: MainAxisAlignment.center,
                                      children: [
                                        Icon(Icons.broken_image, color: Colors.white60),
                                        SizedBox(height: 4),
                                        Text('Load Failed', style: TextStyle(fontSize: 10, color: Colors.white60)),
                                      ],
                                    ),
                                  );
                                },
                                loadingBuilder: (context, child, loadingProgress) {
                                  if (loadingProgress == null) return child;
                                  return Container(
                                    color: Colors.grey[900],
                                    child: const Center(
                                      child: SizedBox(
                                        height: 20,
                                        width: 20,
                                        child: CircularProgressIndicator(strokeWidth: 2),
                                      ),
                                    ),
                                  );
                                },
                              ),
                              // Bottom bar with filename and delete button
                              Positioned(
                                bottom: 0,
                                left: 0,
                                right: 0,
                                child: Container(
                                  padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
                                  decoration: const BoxDecoration(
                                    gradient: LinearGradient(
                                      begin: Alignment.bottomCenter,
                                      end: Alignment.topCenter,
                                      colors: [Colors.black87, Colors.transparent],
                                    ),
                                  ),
                                  child: Row(
                                    mainAxisAlignment: MainAxisAlignment.spaceBetween,
                                    children: [
                                      Expanded(
                                        child: Text(
                                          filename,
                                          style: const TextStyle(color: Colors.white, fontSize: 10),
                                          maxLines: 1,
                                          overflow: TextOverflow.ellipsis,
                                        ),
                                      ),
                                      IconButton(
                                        constraints: const BoxConstraints(),
                                        padding: EdgeInsets.zero,
                                        icon: const Icon(Icons.delete, color: Colors.redAccent, size: 18),
                                        onPressed: () => _deletePhoto(filename),
                                      ),
                                    ],
                                  ),
                                ),
                              ),
                            ],
                          ),
                        );
                      },
                    ),
                  ),
      ),
      floatingActionButton: FloatingActionButton.extended(
        onPressed: _isUploading ? null : _pickAndUploadImage,
        backgroundColor: Colors.deepPurpleAccent,
        foregroundColor: Colors.white,
        icon: _isUploading
            ? const SizedBox(
                height: 18,
                width: 18,
                child: CircularProgressIndicator(color: Colors.white, strokeWidth: 2),
              )
            : const Icon(Icons.add_photo_alternate),
        label: Text(_isUploading ? 'Uploading...' : 'Send Photo'),
      ),
    );
  }
}
