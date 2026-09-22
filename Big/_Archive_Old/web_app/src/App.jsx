import React, { useState, useEffect, useRef } from 'react';
import { UploadCloud, Trash2, Image as ImageIcon, Wifi, AlertCircle, RefreshCw } from 'lucide-react';
import { motion, AnimatePresence } from 'framer-motion';
import './index.css';

function App() {
  const [espIp, setEspIp] = useState('');
  const [photos, setPhotos] = useState([]);
  const [isLoading, setIsLoading] = useState(false);
  const [isUploading, setIsUploading] = useState(false);
  const [errorMsg, setErrorMsg] = useState('');
  const fileInputRef = useRef(null);

  useEffect(() => {
    // 1. Check URL parameters for IP (QR Code strategy)
    const params = new URLSearchParams(window.location.search);
    const ipFromUrl = params.get('ip');
    
    if (ipFromUrl) {
      setEspIp(ipFromUrl);
      localStorage.setItem('esp32_ip', ipFromUrl);
      // Clean up URL without refreshing
      window.history.replaceState({}, document.title, window.location.pathname);
    } else {
      // 2. Fallback to localStorage
      const savedIp = localStorage.getItem('esp32_ip');
      if (savedIp) {
        setEspIp(savedIp);
      }
    }
  }, []);

  useEffect(() => {
    if (espIp) {
      fetchPhotos();
    }
  }, [espIp]);

  const fetchPhotos = async () => {
    setIsLoading(true);
    setErrorMsg('');
    try {
      const response = await fetch(`http://${espIp}/list`);
      if (!response.ok) throw new Error('서버가 바쁩니다.');
      const data = await response.json();
      setPhotos(data);
    } catch (err) {
      setErrorMsg('액자에 연결할 수 없거나 다른 친구가 사용 중이에요! 잠시 후 다시 시도해주세요 😊');
      console.error(err);
    } finally {
      setIsLoading(false);
    }
  };

  const handleUploadClick = () => {
    fileInputRef.current?.click();
  };

  const handleFileChange = async (e) => {
    const file = e.target.files[0];
    if (!file) return;
    
    // Check file size (optional, limit to 10MB for ESP32 safety)
    if (file.size > 10 * 1024 * 1024) {
      setErrorMsg('사진 용량이 너무 큽니다! (최대 10MB)');
      return;
    }

    setIsUploading(true);
    setErrorMsg('');

    try {
      // Convert to JPEG using Canvas (so PNG, WebP etc work on ESP32)
      const img = new Image();
      const objectUrl = URL.createObjectURL(file);
      
      const fileToUpload = await new Promise((resolve, reject) => {
        img.onload = () => {
          URL.revokeObjectURL(objectUrl);
          
          // CYD Screen resolution is exactly 320x240
          const TARGET_WIDTH = 320;
          const TARGET_HEIGHT = 240;
          
          const canvas = document.createElement('canvas');
          canvas.width = TARGET_WIDTH;
          canvas.height = TARGET_HEIGHT;
          
          const ctx = canvas.getContext('2d');
          
          // Fill background with black (letterboxing for different aspect ratios)
          ctx.fillStyle = "#000000";
          ctx.fillRect(0, 0, TARGET_WIDTH, TARGET_HEIGHT);
          
          // Calculate scale to fit image inside 320x240 (object-fit: contain)
          const scale = Math.min(TARGET_WIDTH / img.width, TARGET_HEIGHT / img.height);
          const scaledWidth = img.width * scale;
          const scaledHeight = img.height * scale;
          
          // Center the image
          const dx = (TARGET_WIDTH - scaledWidth) / 2;
          const dy = (TARGET_HEIGHT - scaledHeight) / 2;
          
          // Draw the resized and centered image
          ctx.drawImage(img, dx, dy, scaledWidth, scaledHeight);
          
          canvas.toBlob((blob) => {
            if (blob) {
              // Extract original name without extension and add .jpg
              const newName = file.name.replace(/\.[^/.]+$/, "") + "_" + Date.now() + ".jpg";
              resolve(new File([blob], newName, { type: "image/jpeg" }));
            } else {
              reject(new Error("이미지 변환 실패"));
            }
          }, "image/jpeg", 0.9);
        };
        img.onerror = () => {
          URL.revokeObjectURL(objectUrl);
          reject(new Error("이미지를 읽을 수 없습니다."));
        };
        img.src = objectUrl;
      });
      
      const formData = new FormData();
      formData.append('file', fileToUpload);

      const controller = new AbortController();
      const timeoutId = setTimeout(() => controller.abort(), 60000); 
      
      const response = await fetch(`http://${espIp}/upload`, {
        method: 'POST',
        body: formData,
        signal: controller.signal
      });
      
      clearTimeout(timeoutId);

      if (!response.ok) {
        throw new Error(`HTTP Error ${response.status}`);
      }
      
      fetchPhotos();
    } catch (err) {
      setErrorMsg(`전송 오류: ${err.message} (다시 시도해주세요)`);
      console.error(err);
    } finally {
      setIsUploading(false);
      // Reset input
      if (fileInputRef.current) fileInputRef.current.value = '';
    }
  };

  const handleDelete = async (filename) => {
    if (!confirm('이 사진을 정말 삭제할까요?')) return;
    
    setIsLoading(true);
    try {
      const response = await fetch(`http://${espIp}/delete_get?file=${filename}`);
      if (!response.ok) throw new Error('삭제 실패');
      fetchPhotos();
    } catch (err) {
      setErrorMsg('다른 기기에서 사용 중이어서 삭제할 수 없습니다.');
      console.error(err);
    } finally {
      setIsLoading(false);
    }
  };

  // Render IP Setup Screen if no IP is provided
  if (!espIp) {
    return (
      <>
        <div className="blob-shape blob-1"></div>
        <div className="blob-shape blob-2"></div>
        <div style={{ flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center', padding: '20px' }}>
          <div className="glass-container" style={{ padding: '40px', textAlign: 'center', maxWidth: '400px', width: '100%' }}>
            <Wifi size={48} color="#0885ff" style={{ marginBottom: '20px' }} />
            <h1 style={{ fontSize: '24px', marginBottom: '10px' }}>액자를 찾을 수 없습니다!</h1>
            <p style={{ color: 'var(--text-secondary)', marginBottom: '30px' }}>액자 옆에 있는 QR 코드를 스캔해서 다시 접속해 주세요.</p>
            
            <p style={{ fontSize: '12px', color: 'var(--text-secondary)' }}>
              관리자 전용: 수동 연결
            </p>
            <input 
              type="text" 
              placeholder="192.168.x.x" 
              style={{
                width: '100%', padding: '12px', borderRadius: '12px', border: 'none',
                background: 'rgba(255,255,255,0.1)', color: '#fff', marginTop: '10px',
                textAlign: 'center', outline: 'none'
              }}
              onKeyDown={(e) => {
                if (e.key === 'Enter') {
                  setEspIp(e.target.value);
                  localStorage.setItem('esp32_ip', e.target.value);
                }
              }}
            />
          </div>
        </div>
      </>
    );
  }

  return (
    <>
      <div className="blob-shape blob-1"></div>
      <div className="blob-shape blob-2"></div>
      
      <div style={{ padding: '20px', paddingBottom: '100px', maxWidth: '600px', margin: '0 auto' }}>
        {/* Header */}
        <header style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: '30px', marginTop: '10px' }}>
          <div>
            <h1 style={{ fontSize: '28px', fontWeight: 800 }}>스마트 액자 ✨</h1>
            <p style={{ color: 'var(--text-secondary)', fontSize: '14px' }}>내 사진을 액자에 띄워보세요!</p>
          </div>
          <button 
            onClick={fetchPhotos}
            disabled={isLoading || isUploading}
            style={{ 
              background: 'rgba(255,255,255,0.1)', border: 'none', color: '#fff', 
              padding: '10px', borderRadius: '50%', cursor: 'pointer',
              display: 'flex', alignItems: 'center', justifyContent: 'center'
            }}
          >
            <RefreshCw size={20} className={isLoading ? "loader" : ""} style={{ animation: isLoading ? 'spin 1s linear infinite' : 'none' }} />
          </button>
        </header>

        {/* Error Message */}
        <AnimatePresence>
          {errorMsg && (
            <motion.div 
              initial={{ opacity: 0, y: -20 }} 
              animate={{ opacity: 1, y: 0 }} 
              exit={{ opacity: 0, y: -20 }}
              className="glass-container"
              style={{ padding: '15px 20px', marginBottom: '20px', display: 'flex', alignItems: 'center', gap: '10px', borderLeft: '4px solid #ff4757' }}
            >
              <AlertCircle color="#ff4757" size={24} />
              <p style={{ fontSize: '14px', flex: 1, lineHeight: '1.4' }}>{errorMsg}</p>
            </motion.div>
          )}
        </AnimatePresence>

        {/* Gallery */}
        {photos.length === 0 && !isLoading ? (
          <div className="glass-container" style={{ padding: '60px 20px', textAlign: 'center', opacity: 0.7 }}>
            <ImageIcon size={48} style={{ marginBottom: '15px', opacity: 0.5 }} />
            <p>액자에 아직 사진이 없어요!<br/>아래 버튼을 눌러 사진을 올려보세요.</p>
          </div>
        ) : (
          <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(140px, 1fr))', gap: '16px' }}>
            <AnimatePresence>
              {photos.map((photo, index) => (
                <motion.div 
                  key={photo}
                  initial={{ opacity: 0, scale: 0.9 }}
                  animate={{ opacity: 1, scale: 1 }}
                  exit={{ opacity: 0, scale: 0.9 }}
                  transition={{ delay: index * 0.05 }}
                  className="glass-container"
                  style={{ position: 'relative', overflow: 'hidden', aspectRatio: '1', borderRadius: '16px' }}
                >
                  <img 
                    src={`http://${espIp}/view?file=${photo}`} 
                    alt={photo} 
                    style={{ width: '100%', height: '100%', objectFit: 'cover' }}
                    loading="lazy"
                  />
                  <button 
                    onClick={() => handleDelete(photo)}
                    style={{
                      position: 'absolute', top: '8px', right: '8px',
                      background: 'rgba(0,0,0,0.5)', border: 'none', color: '#fff',
                      width: '32px', height: '32px', borderRadius: '50%',
                      display: 'flex', alignItems: 'center', justifyContent: 'center',
                      backdropFilter: 'blur(4px)', cursor: 'pointer'
                    }}
                  >
                    <Trash2 size={16} color="#ff4757" />
                  </button>
                </motion.div>
              ))}
            </AnimatePresence>
          </div>
        )}

        {/* Upload Button */}
        <div style={{ position: 'fixed', bottom: '30px', left: '50%', transform: 'translateX(-50%)', zIndex: 100 }}>
          <motion.button 
            whileHover={{ scale: 1.05 }}
            whileTap={{ scale: 0.95 }}
            onClick={handleUploadClick}
            disabled={isUploading}
            style={{
              background: 'linear-gradient(135deg, #0885ff, #ff00cc)',
              color: '#fff', border: 'none', borderRadius: '30px',
              padding: '16px 32px', fontSize: '18px', fontWeight: '600',
              display: 'flex', alignItems: 'center', gap: '12px',
              boxShadow: '0 10px 25px rgba(255, 0, 204, 0.4)',
              cursor: 'pointer', opacity: isUploading ? 0.7 : 1
            }}
          >
            {isUploading ? (
              <>
                <span className="loader" style={{ width: '20px', height: '20px', borderWidth: '2px' }}></span>
                전송 중...
              </>
            ) : (
              <>
                <UploadCloud size={24} />
                사진 올리기
              </>
            )}
          </motion.button>
        </div>

        {/* Hidden File Input */}
        <input 
          type="file" 
          accept="image/*" 
          ref={fileInputRef}
          style={{ display: 'none' }}
          onChange={handleFileChange}
        />
      </div>
    </>
  );
}

export default App;
