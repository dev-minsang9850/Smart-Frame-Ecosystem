# Flutter 기본 설정 유지
-keep class io.flutter.app.** { *; }
-keep class io.flutter.plugin.** { *; }
-keep class io.flutter.util.** { *; }
-keep class io.flutter.view.** { *; }
-keep class io.flutter.** { *; }
-keep class io.flutter.plugins.** { *; }

# 이 부분을 본인의 프로젝트 패키지명에 맞게 정확히 고쳐야 합니다.
# 예: dytimetable_flutter 프로젝트라면 보통 com.example.dytimetable_flutter 일 확률이 높습니다.
-keep class com.example.dytimetable_flutter.models.** { *; }

# 만약 @JsonKey 같은 어노테이션을 쓴다면 아래 설정도 추가
-keepattributes Signature, *Annotation*, EnclosingMethod

# Play Core Library 관련 누락 클래스 경고 무시
-dontwarn com.google.android.play.core.**
-keep class com.google.android.play.core.** { *; }

# Flutter 내부에서 참조하는 Play Core 관련 설정
-dontwarn io.flutter.embedding.engine.deferredcomponents.**