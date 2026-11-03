package org.mavlink.qgroundcontrol;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbManager;
import android.os.Build;

import androidx.core.content.ContextCompat;

import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;

/**
 * Titan: USB cameras and capture cards (USB video class devices) for the native UVC driver in src/TitanUsbCamera.
 * Android's camera service lists a USB camera only where the device's camera HAL supports external cameras, so, as
 * other USB camera apps do, this opens the camera through the USB host API and hands its file descriptor to libuvc.
 * Called from C++ on the Qt thread; the receiver runs on the main thread.
 */
public class TitanUsbCameraManager {
    private static final String TAG = "TitanUsbCamera";
    private static final String ACTION_USB_CAMERA_PERMISSION = "org.mavlink.qgroundcontrol.action.USB_CAMERA_PERMISSION";

    /** open() results other than a file descriptor */
    public static final int OPEN_PENDING = -1;  // Waiting for the user to answer the permission dialog
    public static final int OPEN_FAILED = -2;   // Unplugged, or Android would not open it
    public static final int OPEN_DENIED = -3;   // Permission refused; asked again once the camera is plugged back in

    private static final Object lock = new Object();
    private static UsbManager usbManager;
    private static PendingIntent permissionIntent;
    private static final Map<String, UsbDeviceConnection> connections = new HashMap<>();
    private static final Set<String> permissionRequested = new HashSet<>();
    private static final Set<String> permissionDenied = new HashSet<>();

    /** Cameras were plugged in or out, or a permission request was answered */
    private static native void nativeCamerasChanged();

    public static void initialize(final Context context) {
        synchronized (lock) {
            if (usbManager != null) {
                return;
            }

            final Context appContext = context.getApplicationContext();
            usbManager = (UsbManager) appContext.getSystemService(Context.USB_SERVICE);
            if (usbManager == null) {
                QGCLogger.e(TAG, "Failed to get UsbManager");
                return;
            }

            // Mutable so the system can add the device and the answer, which needs an explicit intent from Android 14
            final Intent intent = new Intent(ACTION_USB_CAMERA_PERMISSION).setPackage(appContext.getPackageName());
            int flags = PendingIntent.FLAG_UPDATE_CURRENT;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                flags |= PendingIntent.FLAG_MUTABLE;
            }
            permissionIntent = PendingIntent.getBroadcast(appContext, 0, intent, flags);

            final IntentFilter filter = new IntentFilter();
            filter.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
            filter.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
            filter.addAction(ACTION_USB_CAMERA_PERMISSION);
            ContextCompat.registerReceiver(appContext, receiver, filter, ContextCompat.RECEIVER_NOT_EXPORTED);
        }
    }

    /** Each attached camera as its device name and its label, separated by a newline */
    public static String[] cameras() {
        synchronized (lock) {
            final List<String> result = new ArrayList<>();
            if (usbManager != null) {
                for (final UsbDevice device : usbManager.getDeviceList().values()) {
                    if (isCamera(device)) {
                        result.add(device.getDeviceName() + "\n" + label(device));
                    }
                }
            }
            Collections.sort(result);
            return result.toArray(new String[0]);
        }
    }

    /**
     * Opens a camera by its device name, asking for permission first if need be.
     *
     * @return the device's file descriptor, or OPEN_PENDING, OPEN_FAILED or OPEN_DENIED
     */
    public static int open(final String deviceName) {
        synchronized (lock) {
            if (usbManager == null) {
                return OPEN_FAILED;
            }

            final UsbDevice device = usbManager.getDeviceList().get(deviceName);
            if (device == null) {
                return OPEN_FAILED;
            }

            if (!usbManager.hasPermission(device)) {
                if (permissionDenied.contains(deviceName)) {
                    return OPEN_DENIED;
                }
                if (permissionRequested.add(deviceName)) {
                    QGCLogger.i(TAG, "Requesting permission for " + deviceName);
                    usbManager.requestPermission(device, permissionIntent);
                }
                return OPEN_PENDING;
            }

            closeLocked(deviceName);
            final UsbDeviceConnection connection = usbManager.openDevice(device);
            if (connection == null) {
                QGCLogger.w(TAG, "Failed to open " + deviceName);
                return OPEN_FAILED;
            }
            connections.put(deviceName, connection);
            QGCLogger.i(TAG, "Opened " + deviceName);
            return connection.getFileDescriptor();
        }
    }

    /** Closes a camera once the native driver has finished with its file descriptor */
    public static void close(final String deviceName) {
        synchronized (lock) {
            closeLocked(deviceName);
        }
    }

    private static void closeLocked(final String deviceName) {
        final UsbDeviceConnection connection = connections.remove(deviceName);
        if (connection != null) {
            connection.close();
            QGCLogger.i(TAG, "Closed " + deviceName);
        }
    }

    private static boolean isCamera(final UsbDevice device) {
        if (device.getDeviceClass() == UsbConstants.USB_CLASS_VIDEO) {
            return true;
        }
        for (int i = 0; i < device.getInterfaceCount(); i++) {
            if (device.getInterface(i).getInterfaceClass() == UsbConstants.USB_CLASS_VIDEO) {
                return true;
            }
        }
        return false;
    }

    private static String label(final UsbDevice device) {
        final String product = device.getProductName();
        if (product != null && !product.trim().isEmpty()) {
            return product.trim();
        }
        return String.format(Locale.US, "%04x:%04x", device.getVendorId(), device.getProductId());
    }

    private static UsbDevice deviceFrom(final Intent intent) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            return intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice.class);
        }
        return intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
    }

    private static final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(final Context context, final Intent intent) {
            final String action = intent.getAction();
            final UsbDevice device = deviceFrom(intent);
            boolean changed = false;

            synchronized (lock) {
                if (ACTION_USB_CAMERA_PERMISSION.equals(action)) {
                    if (device != null) {
                        final String name = device.getDeviceName();
                        permissionRequested.remove(name);
                        if (usbManager != null && !usbManager.hasPermission(device)) {
                            QGCLogger.i(TAG, "Permission denied for " + name);
                            permissionDenied.add(name);
                        }
                    } else {
                        permissionRequested.clear();
                    }
                    changed = true;
                } else if (device != null && isCamera(device)) {
                    if (UsbManager.ACTION_USB_DEVICE_DETACHED.equals(action)) {
                        permissionRequested.remove(device.getDeviceName());
                        permissionDenied.remove(device.getDeviceName());
                    }
                    changed = true;
                }
            }

            if (changed) {
                try {
                    nativeCamerasChanged();
                } catch (final UnsatisfiedLinkError e) {
                    QGCLogger.w(TAG, "Native callback not registered", e);
                }
            }
        }
    };
}
