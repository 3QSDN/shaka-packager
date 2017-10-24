Raw key encryption options
^^^^^^^^^^^^^^^^^^^^^^^^^^

--enable_fixed_key_encryption

    Enable encryption with fixed key.

--enable_fixed_key_decryption

    Enable decryption with fixed key.

--keys <key_info_string[,key_info_string][,key_info_string]...>

    **key_info_string** is of the form::

        label={label}:key_id={key_id}:key={key}

    *label* can be an arbitrary string or a predefined DRM label like AUDIO,
    SD, HD, etc. Label with an empty string indicates the default key and
    key_id. The *drm_label* in :doc:`/options/stream_descriptors`,
    which can be implicit, determines which key info is applied to the stream
    by matching the *drm_label* with the *label* in key info.

    *key_id* and *key* should be 32-digit hex strings.

--key_id <32-digit hex string>

    The key id in hex string format. Deprecated. Use --keys instead.

--key <32-digit hex string>

    The key in hex string format. Deprecated. Use --keys instead.

--iv <16-digit or 32-digit hex string>

    IV in hex string format. If not specified, a random IV will be generated.
    This flag should only be used for testing. IV must be either 8 bytes
    (16 digits HEX) or 16 bytes (32 digits in HEX).

--pssh <hex string>

    One or more concatenated PSSH boxes in hex string format. If not specified,
    a `v1 common PSSH box <https://goo.gl/s8RIhr>`_ will be generated.
