# JuanCarlosLegals SIM Alpha Emulator

**Standard JuanCarlosLegals estado Alpha en desarrollo.** Este repositorio contiene
una emulación educativa de una tarjeta tipo SIM escrita en C para aprender cómo
se encadenan conceptos como APDU, ficheros elementales, verificación de PIN,
derivación de claves y una respuesta de autenticación local.

JuanCarlosLegals está **inspirado por la idea general** de arquitecturas SIM y
redes móviles, pero **no es GSMA, no implementa GSMA, no es interoperable con
operadores reales y no debe usarse con credenciales reales**. Los algoritmos de
cifrado incluidos son juguetes didácticos para laboratorio.

## Qué incluye

- Parser de APDU en hexadecimal con soporte para CLA/INS/P1/P2/Lc/Data/Le.
- Sistema de ficheros simulado con MF y EFs de ejemplo:
  - `3F00` MF raíz.
  - `6F07` EF-IMSI protegido por PIN.
  - `6F46` EF-SPN público.
  - `6F3C` EF-SMS escribible tras verificar PIN.
- PIN local por defecto `1234`, contador de reintentos y estado de seguridad.
- Comando de autenticación `INTERNAL AUTHENTICATE` (`INS 88`) con respuesta
  SRES y clave de sesión derivadas mediante primitivas toy de JuanCarlosLegals.
- CLI para ejecutar APDUs individuales o una demo completa.

## Compilar

```sh
make
```

El binario se genera en `build/jcl-sim`.

## Ejecutar demo

```sh
./build/jcl-sim --demo
```

La demo muestra:

1. Estado inicial de la tarjeta simulada.
2. Selección de EF-IMSI.
3. Intento de lectura bloqueado por falta de PIN.
4. Verificación de PIN.
5. Lectura de IMSI de laboratorio.
6. Autenticación toy con RAND.
7. Escritura y lectura de un EF-SMS de prueba.

## APDUs soportadas

| APDU | Descripción |
| --- | --- |
| `00 A4 00 00 02 <FID_hi> <FID_lo>` | SELECT de fichero |
| `00 B0 <off_hi> <off_lo> 00 [Le]` | READ BINARY del fichero seleccionado |
| `00 D6 <off_hi> <off_lo> <Lc> <data>` | UPDATE BINARY del fichero seleccionado |
| `00 20 00 01 <Lc> <PIN ASCII>` | VERIFY PIN |
| `80 88 00 00 <Lc> <RAND>` | Autenticación JuanCarlosLegals toy |
| `80 F2 00 00` | GET STATUS |

Ejemplos:

```sh
./build/jcl-sim '80 F2 00 00'
./build/jcl-sim '00 20 00 01 04 31 32 33 34' '00 A4 00 00 02 6F 07' '00 B0 00 00 00 0F'
```

## Seguridad y límites

Este proyecto es deliberadamente seguro para aprendizaje porque no contiene
parámetros de red reales, no habla con infraestructura móvil, no clona tarjetas,
no calcula algoritmos de operador y no extrae secretos. El cifrado toy (`jcl_mac64`
y `jcl_stream_xor`) solo sirve para observar flujo de datos: RAND → SRES → clave
de sesión simulada.

Si quieres extender el estándar Alpha, hazlo en un entorno aislado y mantén el
alcance en datos sintéticos de laboratorio.

## Probar

```sh
make test
```
