package com.bigpickle.nebulaos;

import android.os.Bundle;

import com.getcapacitor.BridgeActivity;

public class MainActivity extends BridgeActivity {
    @Override
    public void onCreate(Bundle savedInstanceState) {
        // on-device opencode agent: shell, files and streaming HTTPS without a PC bridge
        registerPlugin(NebulaNativePlugin.class);
        registerPlugin(NebulaStoragePlugin.class);
        super.onCreate(savedInstanceState);
    }
}
