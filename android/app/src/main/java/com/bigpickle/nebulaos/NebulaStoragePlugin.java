package com.bigpickle.nebulaos;

import android.app.Activity;
import android.content.Intent;
import android.content.UriPermission;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import androidx.activity.result.ActivityResult;
import com.getcapacitor.JSObject;
import com.getcapacitor.Plugin;
import com.getcapacitor.PluginCall;
import com.getcapacitor.PluginMethod;
import com.getcapacitor.annotation.ActivityCallback;
import com.getcapacitor.annotation.CapacitorPlugin;

/** SAF access to a user-selected directory. This does not change or delete the existing virtual disk. */
@CapacitorPlugin(name = "NebulaStorage")
public class NebulaStoragePlugin extends Plugin {
    private static final String PREFS = "nebula_storage";
    private static final String TREE = "tree_uri";

    private JSObject describe(Uri tree) throws Exception {
        boolean read = false, write = false;
        for (UriPermission p : getContext().getContentResolver().getPersistedUriPermissions()) {
            if (p.getUri().equals(tree)) { read = p.isReadPermission(); write = p.isWritePermission(); break; }
        }
        if (!read || !write) throw new SecurityException("Android не сохранил разрешение на чтение и запись этой папки");
        Uri root = DocumentsContract.buildDocumentUriUsingTree(tree, DocumentsContract.getTreeDocumentId(tree));
        String name = null, mime = null;
        try (Cursor c = getContext().getContentResolver().query(root,
                new String[]{DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE},
                null, null, null)) {
            if (c == null || !c.moveToFirst()) throw new IllegalStateException("Папка недоступна");
            name = c.getString(0); mime = c.getString(1);
        }
        if (!DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) throw new IllegalStateException("Выбранный объект не является папкой");
        JSObject out = new JSObject(); out.put("selected", true); out.put("name", name == null ? "Папка" : name); return out;
    }

    @PluginMethod
    public void folderInfo(PluginCall call) {
        String value = getContext().getSharedPreferences(PREFS, 0).getString(TREE, null);
        if (value == null) { JSObject out = new JSObject(); out.put("selected", false); call.resolve(out); return; }
        try { call.resolve(describe(Uri.parse(value))); }
        catch (Exception e) { JSObject out = new JSObject(); out.put("selected", false); out.put("error", "Папка больше недоступна: " + e.getMessage()); call.resolve(out); }
    }

    @PluginMethod
    public void chooseFolder(PluginCall call) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(call, intent, "folderChosen");
    }

    @ActivityCallback
    private void folderChosen(PluginCall call, ActivityResult result) {
        if (call == null) return;
        Intent data = result.getData();
        if (result.getResultCode() != Activity.RESULT_OK || data == null || data.getData() == null) {
            call.reject("Выбор папки отменён; прежние данные не изменены"); return;
        }
        Uri tree = data.getData();
        int flags = data.getFlags() & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        if (flags != (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION)) {
            call.reject("Android не предоставил доступ на чтение и запись; прежняя папка не изменена"); return;
        }
        try {
            getContext().getContentResolver().takePersistableUriPermission(tree, flags);
            JSObject info = describe(tree);
            if (!getContext().getSharedPreferences(PREFS, 0).edit().putString(TREE, tree.toString()).commit())
                throw new IllegalStateException("Не удалось сохранить выбор папки");
            call.resolve(info);
        } catch (Exception e) { call.reject("Не удалось сохранить доступ к папке: " + e.getMessage()); }
    }
}
