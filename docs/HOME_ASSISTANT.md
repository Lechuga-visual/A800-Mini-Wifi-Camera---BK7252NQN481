# Home Assistant

[Español](HOME_ASSISTANT.es.md) · [README](../README.md)

Complete camera setup first. Download **a800-local-ca.crt** from the camera UI. Set a router DHCP reservation if possible.

## Home Assistant OS and HACS

The stock [MJPEG IP Camera integration](https://www.home-assistant.io/integrations/mjpeg/) supports Basic authentication and SSL verification, but has no per-camera private-CA selector. This setup uses [Additional CA](https://github.com/Athozs/hass-additional-ca), a third-party HACS integration.

1. Install **Additional CA** through HACS.
2. Find the directory containing **configuration.yaml**.
3. Create **additional_ca** directly beside that file. Put the downloaded certificate there.

```text
configuration.yaml
additional_ca/
    a800-local-ca.crt
custom_components/
    additional_ca/
        __init__.py
```

The two `additional_ca` directories serve different purposes: public certificates beside YAML; integration code inside `custom_components`. If your file editor already starts in `/config`, do not create another `config` folder inside it.

Add to `configuration.yaml`, preserving your other settings:

```yaml
additional_ca:
  a800_local: a800-local-ca.crt
```

4. Check configuration and restart Home Assistant.
5. If the log says the CA **“is missing in SSL Context”**, restart Home Assistant again after the CA has been installed. Existing SSL contexts do not reload automatically. Check timestamps; old log entries can remain visible.
6. Configure or reload MJPEG IP Camera.

## Camera entry

Replace `CAMERA_IP` with the address shown in the camera UI.

| Field | Value |
|---|---|
| MJPEG URL | `https://CAMERA_IP/stream.mjpg` |
| Still image URL | `https://CAMERA_IP/snapshot.jpg` |
| Authentication | Basic |
| Username | `ha` |
| Password | The Home Assistant password you set on the camera |
| Verify SSL | **Enabled** |

Stop the camera's web viewer before opening HA video. A snapshot can return 503 while another viewer holds the single video slot. Allow the previous connection to close before retrying.

If video works only with SSL verification disabled, encryption is present but camera identity is not verified. Fix CA trust rather than treating that as finished setup.

## After changes

Changing the camera IP reissues its server certificate under the same CA. Update the URL in HA. Changing the `ha` password requires updating HA's saved password. Resetting camera identity creates a new CA; replace the CA file and repeat trust setup.

After a HA update recreates its container, Additional CA may need another Core restart to refresh cached SSL contexts. Check its repair messages.

The older 0.1 prototype was verified in Home Assistant OS with SSL verification enabled. The new 0.2 on-device onboarding flow still needs its physical-device integration check.
