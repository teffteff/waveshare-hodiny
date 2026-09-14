#!/usr/bin/env node
// Otevře šifrovanou zálohu nastavení hodin (obálka verze 4) bez hodin.
//
//   BACKUP_PASSWORD='…' node tools/decrypt_settings_backup.mjs zaloha.whbackup [vystup.bin]
//
// Ověří heslo i autentizovaný popis a vypíše, co záloha nese. S druhým
// argumentem uloží otevřenou binární zálohu ("WHSB", SettingsBackup.h) - ta
// obsahuje tokeny čitelně, tak s ní podle toho zacházej. Slouží jako nezávislá
// kontrola formátu (SettingsBackupCrypto.h) a jako záchrana, kdyby hodiny,
// které zálohu umí otevřít, nebyly po ruce.

import { createDecipheriv, pbkdf2Sync } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";

const [file, output] = process.argv.slice(2);
const password = process.env.BACKUP_PASSWORD;
if (!file || !password) {
  console.error("Použití: BACKUP_PASSWORD='…' node tools/decrypt_settings_backup.mjs zaloha.whbackup [vystup.bin]");
  process.exit(2);
}

const envelope = JSON.parse(readFileSync(file, "utf8"));
if (envelope.format !== "waveshare-hodiny-settings" || envelope.version !== 4) {
  console.error("Soubor není šifrovaná záloha hodin (obálka verze 4).");
  process.exit(1);
}
if (envelope.cipher !== "AES-256-GCM" || envelope.kdf !== "PBKDF2-SHA256" ||
    !Number.isInteger(envelope.iterations) || envelope.iterations < 10000 ||
    envelope.iterations > 100000) {
  console.error("Nepodporované šifrování nebo počet iterací.");
  process.exit(1);
}

// Musí přesně odpovídat settingsBackupAssociatedData() ve firmwaru.
const associatedData = [
  envelope.format, envelope.version, envelope.firmware, envelope.schema,
  envelope.secrets ? "true" : "false", envelope.exportedAt ?? "",
  envelope.cipher, envelope.kdf, envelope.iterations,
  envelope.salt.toLowerCase(), envelope.nonce.toLowerCase(),
].join("\n");

const sealed = Buffer.from(envelope.data.replaceAll("-", "+").replaceAll("_", "/"), "base64");
const key = pbkdf2Sync(Buffer.from(password, "utf8"), Buffer.from(envelope.salt, "hex"),
                       envelope.iterations, 32, "sha256");
const decipher = createDecipheriv("aes-256-gcm", key, Buffer.from(envelope.nonce, "hex"));
decipher.setAAD(Buffer.from(associatedData, "utf8"));
decipher.setAuthTag(sealed.subarray(sealed.length - 16));
let plain;
try {
  plain = Buffer.concat([decipher.update(sealed.subarray(0, sealed.length - 16)), decipher.final()]);
} catch {
  console.error("Heslo zálohy není správné, nebo je soubor poškozený.");
  process.exit(1);
}

if (plain.subarray(0, 4).toString("latin1") !== "WHSB") {
  console.error("Dešifrovaný obsah není záloha hodin.");
  process.exit(1);
}
const flags = plain[5];
console.log(`firmware ${envelope.firmware}, schéma ${envelope.schema}, ` +
            `export ${envelope.exportedAt || "bez času"}, ${plain.length} B, ` +
            `tajemství: ${flags & 1 ? "ano" : "ne"}`);
if (output) {
  writeFileSync(output, plain, { mode: 0o600 });
  console.log(`Otevřená záloha uložena do ${output}.`);
}
