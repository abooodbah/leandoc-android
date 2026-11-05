package io.github.abooodbah.leandoc;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.Random;
import java.util.concurrent.ConcurrentHashMap;
import java.util.zip.CRC32;
import java.util.zip.Deflater;
import java.util.zip.DeflaterOutputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/**
 * Serves the desktop app's test documents through pipes, as many real
 * providers do, so the app cannot rely on seeking or on a file path. The path
 * is the fixture path, for example features/styles.docx. Names ending in
 * "-nosize" report no size; "huge.docx" streams 65 MiB of zeros, as a broken
 * download would. "large-photos.docx" is features/images.docx with two of
 * its pictures replaced by photo-sized noise images, about 24 MB in all, the
 * size of a report full of phone photos; it is made once into the cache.
 *
 * <p>Written in Java on purpose: Android runs this provider in the test
 * package's own process, where the Kotlin standard library is missing because
 * the build leaves it out of the test APK (the app already has it).
 */
public final class FixtureProvider extends ContentProvider {
    public static final String AUTHORITY = "io.github.abooodbah.leandoc.fixtures";

    /** One MiB past the app's 64 MiB limit. */
    public static final long HUGE_BYTES = 65L * 1024 * 1024;

    /** A valid document of about 24 MB; see the class comment. */
    public static final String LARGE_PHOTOS = "large-photos.docx";
    private static final int PHOTO_WIDTH = 2800;
    private static final int PHOTO_HEIGHT = 1400;

    private static final ConcurrentHashMap<String, Long> WRITTEN = new ConcurrentHashMap<>();

    public static Uri uri(String path) {
        return Uri.parse("content://" + AUTHORITY + "/" + path);
    }

    private interface Sink {
        void write(byte[] chunk, int length) throws IOException;
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public String getType(Uri uri) {
        String path = uri.getPath() == null ? "" : uri.getPath();
        String extension = path.substring(path.lastIndexOf('.') + 1);
        int dash = extension.indexOf('-');
        if (dash >= 0) extension = extension.substring(0, dash);
        switch (extension) {
            case "docx": return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
            case "dotx": return "application/vnd.openxmlformats-officedocument.wordprocessingml.template";
            case "docm": return "application/vnd.ms-word.document.macroenabled.12";
            case "dotm": return "application/vnd.ms-word.template.macroenabled.12";
            case "doc": return "application/msword";
            default: return "application/octet-stream";
        }
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        String path = pathOf(uri);
        String[] columns = projection != null
            ? projection
            : new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE};
        Object[] row = new Object[columns.length];
        for (int i = 0; i < columns.length; i++) {
            if (OpenableColumns.DISPLAY_NAME.equals(columns[i])) {
                row[i] = path.substring(path.lastIndexOf('/') + 1).replace("-nosize", "");
            } else if (OpenableColumns.SIZE.equals(columns[i])) {
                try {
                    row[i] = path.contains("-nosize") ? null : sizeOf(path);
                } catch (IOException e) {
                    throw new IllegalArgumentException(path, e);
                }
            }
        }
        MatrixCursor cursor = new MatrixCursor(columns);
        cursor.addRow(row);
        return cursor;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!"r".equals(mode)) throw new SecurityException("read only");
        String path = pathOf(uri);
        ParcelFileDescriptor[] pipe;
        try {
            sizeOf(path); // Throws for unknown names before a pipe is made.
            pipe = ParcelFileDescriptor.createReliablePipe();
        } catch (IOException e) {
            throw new FileNotFoundException(e.toString());
        }
        WRITTEN.remove(path);
        ParcelFileDescriptor write = pipe[1];
        new Thread(() -> {
            long[] total = {0};
            try (OutputStream out = new ParcelFileDescriptor.AutoCloseOutputStream(write)) {
                write(path, (chunk, length) -> {
                    out.write(chunk, 0, length);
                    total[0] += length;
                });
            } catch (IOException closed) {
                // The reader stopped early, which is what the size limit should do.
            }
            WRITTEN.put(path, total[0]);
        }).start();
        return pipe[0];
    }

    /** "written": how many bytes the last stream of arg delivered, or -1 while it runs. */
    @Override
    public Bundle call(String method, String arg, Bundle extras) {
        if (!"written".equals(method) || arg == null) return null;
        Bundle result = new Bundle();
        Long bytes = WRITTEN.get(arg);
        result.putLong("bytes", bytes == null ? -1 : bytes);
        return result;
    }

    private static String pathOf(Uri uri) {
        String path = uri.getPath();
        if (path == null) throw new IllegalArgumentException(String.valueOf(uri));
        return path.startsWith("/") ? path.substring(1) : path;
    }

    private long sizeOf(String path) throws IOException {
        String clean = path.replace("-nosize", "");
        if (clean.equals("huge.docx")) return HUGE_BYTES;
        // Assets may be compressed in the APK, so count instead of asking.
        long[] total = {0};
        write(clean, (chunk, length) -> total[0] += length);
        return total[0];
    }

    private void write(String path, Sink out) throws IOException {
        String clean = path.replace("-nosize", "");
        byte[] buffer = new byte[64 * 1024];
        if (clean.equals("huge.docx")) {
            for (long sent = 0; sent < HUGE_BYTES; sent += buffer.length) out.write(buffer, buffer.length);
            return;
        }
        try (InputStream input = clean.equals(LARGE_PHOTOS)
                ? new FileInputStream(largePhotos())
                : getContext().getAssets().open("fixtures/" + clean)) {
            int read;
            while ((read = input.read(buffer)) >= 0) out.write(buffer, read);
        }
    }

    private synchronized File largePhotos() throws IOException {
        File file = new File(getContext().getCacheDir(), LARGE_PHOTOS);
        if (file.isFile()) return file;
        File partial = new File(getContext().getCacheDir(), LARGE_PHOTOS + ".part");
        Random random = new Random(20261105L);
        try (ZipInputStream in = new ZipInputStream(getContext().getAssets().open("fixtures/features/images.docx"));
             ZipOutputStream out = new ZipOutputStream(new FileOutputStream(partial))) {
            byte[] buffer = new byte[64 * 1024];
            for (ZipEntry entry; (entry = in.getNextEntry()) != null; ) {
                String name = entry.getName();
                boolean photo = name.equals("word/media/image1.png") || name.equals("word/media/image3.png");
                // Noise does not compress; storing it saves the work of trying.
                out.setLevel(photo ? Deflater.NO_COMPRESSION : Deflater.DEFAULT_COMPRESSION);
                out.putNextEntry(new ZipEntry(name));
                if (photo) {
                    writeNoisePng(out, PHOTO_WIDTH, PHOTO_HEIGHT, random);
                } else {
                    for (int read; (read = in.read(buffer)) >= 0; ) out.write(buffer, 0, read);
                }
                out.closeEntry();
            }
        }
        if (!partial.renameTo(file)) throw new IOException("could not keep " + file);
        return file;
    }

    /** An RGB PNG of random pixels, written with stored (uncompressed) zlib blocks. */
    private static void writeNoisePng(OutputStream target, int width, int height, Random random) throws IOException {
        DataOutputStream out = new DataOutputStream(target);
        out.write(new byte[] {(byte) 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'});
        ByteArrayOutputStream header = new ByteArrayOutputStream();
        DataOutputStream fields = new DataOutputStream(header);
        fields.writeInt(width);
        fields.writeInt(height);
        fields.write(new byte[] {8, 2, 0, 0, 0}); // 8-bit RGB, no interlace.
        chunk(out, "IHDR", header.toByteArray());
        ByteArrayOutputStream pixels = new ByteArrayOutputStream(64 * 1024 + 4096);
        Deflater deflater = new Deflater(Deflater.NO_COMPRESSION);
        byte[] row = new byte[1 + width * 3];
        try (DeflaterOutputStream zlib = new DeflaterOutputStream(pixels, deflater, 64 * 1024)) {
            for (int y = 0; y < height; y++) {
                random.nextBytes(row);
                row[0] = 0; // Filter type: none.
                zlib.write(row);
                if (pixels.size() >= 64 * 1024) {
                    chunk(out, "IDAT", pixels.toByteArray());
                    pixels.reset();
                }
            }
        } finally {
            deflater.end();
        }
        chunk(out, "IDAT", pixels.toByteArray());
        chunk(out, "IEND", new byte[0]);
        out.flush();
    }

    private static void chunk(DataOutputStream out, String type, byte[] data) throws IOException {
        byte[] name = type.getBytes(StandardCharsets.US_ASCII);
        CRC32 crc = new CRC32();
        crc.update(name);
        crc.update(data);
        out.writeInt(data.length);
        out.write(name);
        out.write(data);
        out.writeInt((int) crc.getValue());
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        return 0;
    }
}
