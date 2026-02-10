package io.github.kulitorum.decenza_de1;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;

import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

/**
 * Helper for external storage permission.
 * - Android 9-10: Requests WRITE_EXTERNAL_STORAGE at runtime
 * - Android 11+: Opens settings for MANAGE_EXTERNAL_STORAGE
 * This allows direct file access to Documents/Decenza for profile storage.
 */
public class StorageHelper {
    private static final String TAG = "StorageHelper";
    private static final int PERMISSION_REQUEST_CODE = 100;
    private static Activity sActivity;

    public static void init(Activity activity) {
        sActivity = activity;
    }

    /**
     * Check if we have external storage permission.
     * On Android 9-10, checks WRITE_EXTERNAL_STORAGE runtime permission.
     * On Android 11+, checks MANAGE_EXTERNAL_STORAGE.
     */
    public static boolean hasStoragePermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // Android 11+ (API 30+)
            return Environment.isExternalStorageManager();
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            // Android 6-10 (API 23-29) - check runtime permission
            if (sActivity == null) return false;
            return ContextCompat.checkSelfPermission(sActivity,
                Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
        } else {
            // Android 5 and below - permission granted at install
            return true;
        }
    }

    /**
     * Request storage permission.
     * On Android 9-10: Shows runtime permission dialog.
     * On Android 11+: Opens system settings.
     */
    public static void requestStoragePermission() {
        if (sActivity == null) {
            Log.e(TAG, "Activity not initialized");
            return;
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // Android 11+ - open settings for MANAGE_EXTERNAL_STORAGE
            try {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                Uri uri = Uri.fromParts("package", sActivity.getPackageName(), null);
                intent.setData(uri);
                sActivity.startActivity(intent);
                Log.i(TAG, "Opened storage permission settings");
            } catch (Exception e) {
                Log.w(TAG, "Could not open app-specific settings, trying general: " + e.getMessage());
                try {
                    Intent intent = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                    sActivity.startActivity(intent);
                } catch (Exception e2) {
                    Log.e(TAG, "Could not open storage settings: " + e2.getMessage());
                }
            }
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            // Android 6-10 - request runtime permission
            ActivityCompat.requestPermissions(sActivity,
                new String[]{Manifest.permission.WRITE_EXTERNAL_STORAGE},
                PERMISSION_REQUEST_CODE);
            Log.i(TAG, "Requested WRITE_EXTERNAL_STORAGE permission");
        }
    }

    /**
     * Get the path to the profiles directory (Documents/Decenza).
     * Creates the directory if it doesn't exist.
     */
    public static String getProfilesPath() {
        // Use public Documents directory
        java.io.File documentsDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS);
        java.io.File profilesDir = new java.io.File(documentsDir, "Decenza");

        if (!profilesDir.exists()) {
            if (profilesDir.mkdirs()) {
                Log.i(TAG, "Created profiles directory: " + profilesDir.getAbsolutePath());
            } else {
                Log.e(TAG, "Failed to create profiles directory: " + profilesDir.getAbsolutePath());
            }
        }

        return profilesDir.getAbsolutePath();
    }

    /**
     * Get the path to the backups directory (Documents/Decenza Backups).
     * Creates the directory if it doesn't exist.
     */
    public static String getBackupsPath() {
        // Use public Documents directory
        java.io.File documentsDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS);
        java.io.File backupsDir = new java.io.File(documentsDir, "Decenza Backups");

        if (!backupsDir.exists()) {
            if (backupsDir.mkdirs()) {
                Log.i(TAG, "Created backups directory: " + backupsDir.getAbsolutePath());
            } else {
                Log.e(TAG, "Failed to create backups directory: " + backupsDir.getAbsolutePath());
            }
        }

        Log.i(TAG, "Backups directory: " + backupsDir.getAbsolutePath());
        return backupsDir.getAbsolutePath();
    }

    /**
     * Check if we need to request storage permission.
     * Returns true for Android 6+ where runtime permission is needed.
     */
    public static boolean needsStoragePermission() {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.M;
    }

    /**
     * Notify the media scanner about a new file so it appears in the Files app.
     * Call this after creating a file in shared storage (Documents, Downloads, etc).
     */
    public static void scanFile(String filePath) {
        if (sActivity == null) {
            Log.w(TAG, "Activity not initialized, cannot scan file");
            return;
        }

        try {
            // Determine MIME type from file extension
            final String mimeType;
            if (filePath.endsWith(".zip")) {
                mimeType = "application/zip";
            } else if (filePath.endsWith(".db")) {
                mimeType = "application/x-sqlite3";
            } else if (filePath.endsWith(".txt")) {
                mimeType = "text/plain";
            } else {
                mimeType = null;
            }

            android.media.MediaScannerConnection.scanFile(
                sActivity,
                new String[]{filePath},
                new String[]{mimeType},  // Specify MIME type for proper recognition
                new android.media.MediaScannerConnection.OnScanCompletedListener() {
                    @Override
                    public void onScanCompleted(String path, Uri uri) {
                        Log.i(TAG, "Media scan completed for: " + path + " (type: " + mimeType + ")");
                    }
                }
            );
        } catch (Exception e) {
            Log.e(TAG, "Failed to scan file: " + e.getMessage());
        }
    }

    /**
     * Copy a file from source to destination using Java file I/O.
     * This works with Android scoped storage where Qt's QFile::copy() might fail.
     * Returns true on success, false on error.
     */
    public static boolean copyFile(String sourcePath, String destPath) {
        java.io.FileInputStream fis = null;
        java.io.FileOutputStream fos = null;

        try {
            java.io.File sourceFile = new java.io.File(sourcePath);
            java.io.File destFile = new java.io.File(destPath);

            if (!sourceFile.exists()) {
                Log.e(TAG, "Source file does not exist: " + sourcePath);
                return false;
            }

            // Delete destination if it exists
            if (destFile.exists()) {
                destFile.delete();
            }

            fis = new java.io.FileInputStream(sourceFile);
            fos = new java.io.FileOutputStream(destFile);

            byte[] buffer = new byte[4096];
            int length;
            while ((length = fis.read(buffer)) > 0) {
                fos.write(buffer, 0, length);
            }

            fos.flush();

            Log.i(TAG, "Copied file: " + sourcePath + " -> " + destPath);
            return true;

        } catch (Exception e) {
            Log.e(TAG, "Failed to copy file: " + e.getMessage());
            return false;
        } finally {
            // Ensure streams are always closed
            try {
                if (fos != null) fos.close();
            } catch (Exception e) {
                Log.w(TAG, "Failed to close output stream: " + e.getMessage());
            }
            try {
                if (fis != null) fis.close();
            } catch (Exception e) {
                Log.w(TAG, "Failed to close input stream: " + e.getMessage());
            }
        }
    }

    /**
     * Extract a ZIP file to a destination path.
     * Returns the path to the extracted file, or empty string on error.
     */
    public static String unzipFile(String zipFilePath, String destFilePath) {
        java.io.FileInputStream fis = null;
        java.util.zip.ZipInputStream zis = null;
        java.io.FileOutputStream fos = null;

        try {
            java.io.File zipFile = new java.io.File(zipFilePath);
            if (!zipFile.exists()) {
                Log.e(TAG, "ZIP file does not exist: " + zipFilePath);
                return "";
            }

            fis = new java.io.FileInputStream(zipFile);
            zis = new java.util.zip.ZipInputStream(fis);

            java.util.zip.ZipEntry entry = zis.getNextEntry();
            if (entry == null) {
                Log.e(TAG, "ZIP file is empty: " + zipFilePath);
                return "";
            }

            // Extract first file in ZIP to destination
            fos = new java.io.FileOutputStream(destFilePath);

            byte[] buffer = new byte[4096];
            int length;
            while ((length = zis.read(buffer)) > 0) {
                fos.write(buffer, 0, length);
            }

            Log.i(TAG, "Extracted ZIP file: " + zipFilePath + " -> " + destFilePath);
            return destFilePath;

        } catch (Exception e) {
            Log.e(TAG, "Failed to extract ZIP file: " + e.getMessage());
            return "";
        } finally {
            try {
                if (fos != null) fos.close();
            } catch (Exception e) {
                Log.w(TAG, "Failed to close output stream: " + e.getMessage());
            }
            try {
                if (zis != null) {
                    zis.closeEntry();
                    zis.close();
                }
            } catch (Exception e) {
                Log.w(TAG, "Failed to close zip stream: " + e.getMessage());
            }
            try {
                if (fis != null) fis.close();
            } catch (Exception e) {
                Log.w(TAG, "Failed to close input stream: " + e.getMessage());
            }
        }
    }

    /**
     * Create a ZIP file containing the source file.
     * Returns the path to the created ZIP file, or empty string on error.
     */
    public static String zipFile(String sourceFilePath, String zipFilePath) {
        java.io.FileInputStream fis = null;
        java.io.FileOutputStream fos = null;
        java.util.zip.ZipOutputStream zos = null;

        try {
            java.io.File sourceFile = new java.io.File(sourceFilePath);
            if (!sourceFile.exists()) {
                Log.e(TAG, "Source file does not exist: " + sourceFilePath);
                return "";
            }

            fis = new java.io.FileInputStream(sourceFile);
            fos = new java.io.FileOutputStream(zipFilePath);
            zos = new java.util.zip.ZipOutputStream(fos);

            // Set maximum compression level (9 = best compression, slower)
            zos.setLevel(java.util.zip.Deflater.BEST_COMPRESSION);

            // Add file to ZIP with just the filename (not full path)
            java.util.zip.ZipEntry zipEntry = new java.util.zip.ZipEntry(sourceFile.getName());
            zos.putNextEntry(zipEntry);

            // Copy file content to ZIP
            byte[] buffer = new byte[1024];
            int length;
            while ((length = fis.read(buffer)) > 0) {
                zos.write(buffer, 0, length);
            }

            Log.i(TAG, "Created ZIP file: " + zipFilePath);
            return zipFilePath;

        } catch (Exception e) {
            Log.e(TAG, "Failed to create ZIP file: " + e.getMessage());
            return "";
        } finally {
            try {
                if (zos != null) {
                    zos.closeEntry();
                    zos.close();
                }
            } catch (Exception e) {
                Log.w(TAG, "Failed to close zip stream: " + e.getMessage());
            }
            try {
                if (fos != null) fos.close();
            } catch (Exception e) {
                Log.w(TAG, "Failed to close output stream: " + e.getMessage());
            }
            try {
                if (fis != null) fis.close();
            } catch (Exception e) {
                Log.w(TAG, "Failed to close input stream: " + e.getMessage());
            }
        }
    }
}
