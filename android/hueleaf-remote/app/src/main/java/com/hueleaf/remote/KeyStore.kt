package com.hueleaf.remote

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import org.json.JSONObject
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

class KeyStore(private val context: Context) {
    private val prefs = context.getSharedPreferences("remote_key", Context.MODE_PRIVATE)
    private val alias = "hueleaf_remote_key_v1"
    private fun key(): SecretKey {
        val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
        (store.getKey(alias, null) as? SecretKey)?.let { return it }
        val generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore")
        generator.init(KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build())
        return generator.generateKey()
    }
    fun setPin(pin: String) { save(Protocol.pinKey(pin)) }
    fun import(json: String) {
        val value = JSONObject(json)
        require(value.getInt("version") == 1)
        val hex = value.getString("key_hex")
        require(hex.matches(Regex("[0-9a-fA-F]{64}"))) { "密钥文件格式错误" }
        val bytes = ByteArray(32) { hex.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
        save(bytes)
    }
    private fun save(bytes: ByteArray) {
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, key())
        val encoded = cipher.iv + cipher.doFinal(bytes)
        bytes.fill(0)
        check(prefs.edit().putString("key", Base64.encodeToString(encoded, Base64.NO_WRAP)).commit())
    }
    fun load(): ByteArray? {
        val encoded = prefs.getString("key", null) ?: return null
        val data = Base64.decode(encoded, Base64.NO_WRAP)
        require(data.size == 60) { "已保存的密钥损坏" }
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.DECRYPT_MODE, key(), GCMParameterSpec(128, data.copyOfRange(0, 12)))
        return cipher.doFinal(data.copyOfRange(12, data.size))
    }
}
