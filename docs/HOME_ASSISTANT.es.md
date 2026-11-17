# Home Assistant

[English](HOME_ASSISTANT.md) · [README](../README.es.md)

Completa primero la configuración de la cámara. Descarga **a800-local-ca.crt** desde su interfaz. Conviene reservar la IP en el DHCP del router.

## Home Assistant OS con HACS

[MJPEG IP Camera](https://www.home-assistant.io/integrations/mjpeg/) permite Basic y verificar SSL, pero no seleccionar una CA privada por cámara. Aquí usamos [Additional CA](https://github.com/Athozs/hass-additional-ca), una integración de terceros disponible en HACS.

1. Instala **Additional CA** desde HACS.
2. Ubica la carpeta donde está **configuration.yaml**.
3. Crea **additional_ca** justo al lado del YAML y guarda el certificado allí.

```text
configuration.yaml
additional_ca/
    a800-local-ca.crt
custom_components/
    additional_ca/
        __init__.py
```

Las dos carpetas tienen funciones distintas: certificados junto al YAML; código de la integración dentro de `custom_components`. Si el editor ya abre en `/config`, no crees otra carpeta `config` dentro.

Añade esto a `configuration.yaml`, conservando el resto:

```yaml
additional_ca:
  a800_local: a800-local-ca.crt
```

4. Comprueba la configuración y reinicia Home Assistant.
5. Si aparece **“is missing in SSL Context”**, vuelve a reiniciar Home Assistant después de cargar la CA. Los contextos SSL ya creados no se actualizan solos. Comprueba la hora del error: un registro antiguo puede seguir visible.
6. Configura o recarga MJPEG IP Camera.

## Entrada de cámara

Sustituye `IP_CAMARA` por la IP que muestra la cámara.

| Campo | Valor |
|---|---|
| URL MJPEG | `https://IP_CAMARA/stream.mjpg` |
| URL de imagen fija | `https://IP_CAMARA/snapshot.jpg` |
| Autenticación | Basic |
| Usuario | `ha` |
| Contraseña | La de Home Assistant que guardaste en la cámara |
| Verificar SSL | **Activado** |

Detén el visor web de la cámara antes de abrir vídeo en HA. Solo hay una conexión de vídeo simultánea; las imágenes fijas pueden responder 503 mientras esté ocupada. Espera al cierre de la conexión anterior antes de reintentar.

Si solo funciona desactivando la verificación SSL, hay cifrado pero HA no verifica la identidad de la cámara. La configuración no está terminada hasta resolver la confianza de la CA.

## Después de cambiar algo

Cambiar de IP renueva el certificado de servidor con la misma CA; actualiza la URL de HA. Cambiar la contraseña `ha` exige actualizarla en HA. Reiniciar la identidad genera una CA nueva: reemplaza el certificado e importa de nuevo.

Si una actualización recrea el contenedor de HA, Additional CA puede pedir otro reinicio de Core. Revisa sus avisos de reparación.

El prototipo anterior 0.1 se verificó en Home Assistant OS con validación SSL activada. Falta comprobar la integración física del nuevo flujo de primer arranque 0.2.
