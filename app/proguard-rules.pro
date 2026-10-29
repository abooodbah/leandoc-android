# The app has no reflection or serialization. Native and JavaScript entry
# points get their keep rules next to the code that needs them.

# Created from native code (leandoc_jni.cpp).
-keep class io.github.abooodbah.leandoc.OpenedDocument {
    <init>(byte[], byte[]);
}
