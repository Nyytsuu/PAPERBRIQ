package com.paperbriq.app.data;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;

import java.nio.charset.StandardCharsets;
import java.security.KeyStore;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

public final class MachineKeyStore {
    private static final String KEYSTORE = "AndroidKeyStore";
    private static final String ALIAS = "paperbriq_demo_machine_key";
    private static final String TRANSFORMATION = "AES/GCM/NoPadding";
    private final SharedPreferences preferences;

    public MachineKeyStore(Context context) {
        preferences = context.getSharedPreferences("paperbriq_machine_access", Context.MODE_PRIVATE);
    }

    public void save(String origin, String value) throws Exception {
        SecretKey key = loadKey();
        if (key == null) {
            KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE);
            generator.init(new KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .build());
            key = generator.generateKey();
        }
        Cipher cipher = Cipher.getInstance(TRANSFORMATION);
        cipher.init(Cipher.ENCRYPT_MODE, key);
        cipher.updateAAD(origin.getBytes(StandardCharsets.UTF_8));
        byte[] encrypted = cipher.doFinal(value.getBytes(StandardCharsets.UTF_8));
        if (!preferences.edit()
                .putString("iv", Base64.encodeToString(cipher.getIV(), Base64.NO_WRAP))
                .putString("key", Base64.encodeToString(encrypted, Base64.NO_WRAP))
                .commit()) {
            throw new IllegalStateException("Could not save the machine key on this phone.");
        }
    }

    public String read(String origin) {
        String encrypted = preferences.getString("key", "");
        if (encrypted.isEmpty()) return "";
        try {
            Cipher cipher = Cipher.getInstance(TRANSFORMATION);
            byte[] iv = Base64.decode(preferences.getString("iv", ""), Base64.NO_WRAP);
            cipher.init(Cipher.DECRYPT_MODE, loadKey(), new GCMParameterSpec(128, iv));
            cipher.updateAAD(origin.getBytes(StandardCharsets.UTF_8));
            return new String(cipher.doFinal(Base64.decode(encrypted, Base64.NO_WRAP)), StandardCharsets.UTF_8);
        } catch (Exception ignored) {
            return ""; // Restored backups and different app origins require setup again.
        }
    }

    public void clear() {
        preferences.edit().clear().apply();
    }

    private SecretKey loadKey() throws Exception {
        KeyStore store = KeyStore.getInstance(KEYSTORE);
        store.load(null);
        return (SecretKey) store.getKey(ALIAS, null);
    }
}
