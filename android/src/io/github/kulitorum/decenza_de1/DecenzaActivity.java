package io.github.kulitorum.decenza_de1;

import android.content.Intent;
import android.os.Bundle;

import org.qtproject.qt.android.bindings.QtActivity;

public class DecenzaActivity extends QtActivity {

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        StorageHelper.init(this);

        // Start the shutdown service so onTaskRemoved() will be called
        // when the app is swiped away from recent tasks
        try {
            Intent serviceIntent = new Intent(this, DeviceShutdownService.class);
            startService(serviceIntent);
        } catch (IllegalStateException e) {
            // Android may block startService() if app is considered "in background"
            // This can happen during certain wake scenarios - safe to ignore
            android.util.Log.w("DecenzaActivity", "Could not start shutdown service: " + e.getMessage());
        }
    }
}
