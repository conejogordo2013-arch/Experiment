# JuanCarlosLegals SIM Alpha Emulator

**Standard JuanCarlosLegals estado Alpha en desarrollo.** Este repositorio contiene
una emulación educativa de una tarjeta tipo SIM escrita en C para aprender cómo
se encadenan conceptos como hardware de tarjeta, contactos eléctricos, CPU,
ROM, EEPROM, RAM, APDU, ficheros elementales, verificación de PIN, derivación
de claves y una respuesta de autenticación local.

JuanCarlosLegals está **inspirado por la idea general** de arquitecturas SIM y
redes móviles, pero **no es GSMA, no implementa GSMA, no es interoperable con
operadores reales y no debe usarse con credenciales reales**. Los algoritmos de
cifrado incluidos son juguetes didácticos para laboratorio.

## Qué emula

La meta Alpha ya no es solo responder APDUs: el proceso APDU se ejecuta encima
de una tarjeta JuanCarlosLegals completa y sintética.

- **Contactos/bus**: VCC, RST, CLK, IO, frecuencia de reloj, contador de flancos
  y reset frío/caliente.
- **CPU tiny-card**: registros `PC`, `SP`, `ACC`, `FLAGS`, flag `HALT`, ciclos y
  un microconjunto de instrucciones de ROM (`LDI`, `LOAD_RAM`, `STORE_RAM`,
  `XOR`, `ADD`, `JNZ`, `HALT`).
- **ROM**: firmware de arranque fijo con ATR JuanCarlosLegals Alpha.
- **EEPROM**: almacenamiento persistente simulado para ficheros, clave sintética
  de laboratorio y contador de desgaste por byte.
- **RAM**: memoria volátil que el firmware y las APDUs de laboratorio pueden leer
  o escribir tras autenticar PIN.
- **Sistema de ficheros** simulado con MF y EFs de ejemplo:
  - `3F00` MF raíz.
  - `6F07` EF-IMSI protegido por PIN.
  - `6F46` EF-SPN público.
  - `6F3C` EF-SMS escribible tras verificar PIN.
- **Seguridad local**: PIN por defecto `1234`, contador de reintentos y estado de
  seguridad.
- **Core secreto JCI**: `JCCS` raíz sintético de 48 bytes, `MASK` de destrucción,
  derivación por etiquetas y borrado lógico al entrar en `JC_BRICKED`.
- **Identidad JuanCarlosLegals**: `JCD`, `CCC`, `CJC`, `JCC` y `JCID` sintéticos,
  almacenados/mirrored en EEPROM de laboratorio.
- **Red JCI**: `JCVL` dinámico, claves de mantenimiento/dominio/flags/destrucción
  (`JCm`, `JCd`, `JCf`, `JCz`) y derivación `JKHO` tipo OPc ficticio.
- **Ciclo de vida completo**: `BOOT`, `READY`, `AUTH_PENDING`, `AUTH_FAILED`,
  `ACTIVE`, `TEMP_BLOCKED`, `MAINTENANCE`, `LOCKED` y `JC_BRICKED`.
- **Autenticación toy JCI**: `AUTH1` emite `JCDL`; `AUTH2` calcula capas ficticias
  `JC132`, `JCCC`, `JA`, `KJ`, `JCLSC_1`, `JCLSC_2` y `JCx` para canal seguro.
- **Autenticación clásica toy**: comando `INTERNAL AUTHENTICATE` (`INS 88`) con
  respuesta SRES y clave de sesión derivadas mediante primitivas toy antiguas.

## Compilar

```sh
make
```

El binario se genera en `build/jcl-sim`.

## Ejecutar demo completa

```sh
./build/jcl-sim --demo
```

La demo muestra:

1. Reset de tarjeta y ATR Alpha.
2. Flujo ISO-7816-JCI ficticio: `STATE`, `JCR`, `JCID`, `JCC`, `AUTH1`, `AUTH2`
   y canal seguro `SEND`.
3. Estado de CPU y mapa ROM/EEPROM/RAM.
4. Lectura de ROM de arranque.
5. Estado general de SIM + hardware.
6. Selección de EF-IMSI.
7. Intento de lectura bloqueado por falta de PIN.
8. Verificación `JNS` del estándar JuanCarlosLegals.
9. Lectura/escritura de RAM de laboratorio.
10. Lectura de IMSI de laboratorio.
11. Autenticación toy clásica con RAND.
12. Escritura y lectura de un EF-SMS de prueba.
13. Lectura directa de EEPROM para comprobar que el EF-SMS quedó persistido en memoria simulada.

## APDUs soportadas

| APDU | Descripción |
| --- | --- |
| `00 A4 00 00 02 <FID_hi> <FID_lo>` | SELECT de fichero |
| `00 B0 <off_hi> <off_lo> 00 [Le]` | READ BINARY del fichero seleccionado |
| `00 D6 <off_hi> <off_lo> <Lc> <data>` | UPDATE BINARY del fichero seleccionado |
| `00 20 00 01 <Lc> <PIN ASCII>` | VERIFY PIN |
| `80 88 00 00 <Lc> <RAND>` | Autenticación JuanCarlosLegals toy clásica |
| `80 70 00 00 <Lc> <JCI command>` | Entrada ISO-7816-JCI ficticia, también disponible como `JCI:<command>` en la CLI |
| `80 CA 00 01` | GET DATA: ATR |
| `80 CA 00 02` | GET DATA: registros de CPU |
| `80 CA 00 03` | GET DATA: mapa de memoria |
| `80 CA 00 04` | GET DATA: contactos/bus |
| `80 E0 <area> <offset> 01 <len>` | READ MEMORY; área `00` ROM, `01` EEPROM, `02` RAM |
| `80 E2 <area> <offset> <Lc> <data>` | WRITE MEMORY; solo EEPROM/RAM y requiere PIN |
| `80 F0 00 00` | RESET frío y devolución de ATR |
| `80 F1 <cycles_hi> <cycles_lo>` | Avanza ciclos de CPU/reloj |
| `80 F2 00 00` | GET STATUS general SIM + hardware |

Ejemplos:

```sh
./build/jcl-sim '80 F0 00 00' '80 CA 00 02' '80 E0 00 00 01 10'
./build/jcl-sim 'JCI:STATE' 'JCI:AUTH1' 'JCI:AUTH2' 'JCI:SENDhola-jci'
./build/jcl-sim 'JCI:JNS0000000000000000' '80 E2 02 20 04 54 45 53 54' '80 E0 02 20 01 04'
./build/jcl-sim '00 20 00 01 04 31 32 33 34' '00 A4 00 00 02 6F 07' '00 B0 00 00 00 0F'
```


## ISO-7816-JCI ficticio

Además de APDUs hexadecimales, la CLI acepta comandos de laboratorio con el prefijo
`JCI:`. Internamente se traducen a `CLA=80 INS=70` con el comando ASCII en `Data`.

| Comando CLI | Resultado |
| --- | --- |
| `JCI:STATE` | Devuelve el estado de vida (`READY`, `ACTIVE`, etc.) |
| `JCI:JCR` | Devuelve el registro ROM `JCR` sintético |
| `JCI:JCID` | Devuelve identidad corta del cliente/tarjeta |
| `JCI:JCC` | Devuelve `JCD + CCC + CJC` |
| `JCI:AUTH1` | Genera `JCDL` y pasa a `AUTH_PENDING` |
| `JCI:AUTH2` | Consume el `JCDL` en RAM, deriva `JA/KJ/JCLSC_1/JCLSC_2/JCx` y pasa a `ACTIVE` |
| `JCI:JNS0000000000000000` | Verifica el PIN JCI por defecto y habilita lecturas protegidas |
| `JCI:JUL00000000000000000000000000000000` | Verifica PUK JCI por defecto y desbloquea estado `LOCKED` |
| `JCI:SEND<data>` | Aplica canal seguro toy `JV6` usando `JCx` si la tarjeta está `ACTIVE` |
| `JCI:NETTAP:JCCS` | Simula interceptación de red intentando capturar `JCCS`; no revela la clave y activa `JC_BRICKED` |
| `JCI:EXFIL:JCCS` | Alias de prueba para el mismo evento destructivo de exfiltración de `JCCS` |

## Seguridad y límites

Este proyecto es deliberadamente seguro para aprendizaje porque no contiene
parámetros de red reales, no habla con infraestructura móvil, no clona tarjetas,
no calcula algoritmos de operador y no extrae secretos. El cifrado toy (`jcl_mac64`
y `jcl_stream_xor`) solo sirve para observar flujo de datos: RAND → SRES → clave
de sesión simulada.

La CPU, ROM, EEPROM, RAM y bus son modelos de laboratorio para entender capas,
no una reproducción de silicio real. La prueba `JCI:NETTAP:JCCS` existe solo para
simular un detector defensivo: nunca devuelve `JCCS`; borra el core lógico y deja
la tarjeta en `JC_BRICKED`. Si quieres extender el estándar Alpha, hazlo en un
entorno aislado y mantén el alcance en datos sintéticos.

## Probar

```sh
make test
```
