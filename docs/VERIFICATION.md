# Verificación

Este archivo registra únicamente comprobaciones ejecutadas realmente.

## Entorno

* Ubuntu 26.04.1 LTS (x86_64), gcc 15.2.0, Meson 1.10.1, Ninja 1.13.2.
* wlroots 0.20.2 compilado desde `subprojects/wlroots.wrap`.
* Opciones de Meson: `-Dtest=enabled`; XWayland, renderer GLES2, NLS y página
  de hombre (`scdoc`) habilitados.

## Fase 1

| Comprobación | Resultado |
| --- | --- |
| `meson setup build-stellar . -Dtest=enabled` | Correcto |
| `ninja -C build-stellar` | Correcto, ejecutable `stellar-wim` enlazado |
| `meson test -C build-stellar --print-errorlogs` | 131/131 pruebas OK en ese momento (incluye `test_identity_config_dir`) |
| `stellar-wim --version` | `Stellar WiM 0.1.0 (based on Labwc 0.20.3) ... wlroots-0.20.2` |
| `stellar-wim --help` | Muestra `Usage: stellar-wim ...` |
| Ejecución real `WLR_BACKENDS=headless stellar-wim -d` | El compositor inició, creó salida 1280x720, renderizó escenas y permaneció vivo hasta el `timeout` (salida 124 esperada) |
| Prioridad de configuración | `test_identity_config_dir` confirma que lee `stellar-wim/rc.xml` antes que `labwc/rc.xml` |
| Compatibilidad | La ejecución manual leyó `~/.config/labwc/{rc.xml,environment,autostart,themerc-override}` cuando no existía `~/.config/stellar-wim` |

## Fase 2 — Stellar IPC

| Comprobación | Resultado |
| --- | --- |
| `meson setup build-stellar --reconfigure` | Correcto, proyecto `stellar-wim 0.2.0` |
| `ninja -C build-stellar` | Correcto; enlaza `stellar-wim` y `clients/stellar-wimctl` y genera las páginas de hombre nuevas |
| `scdoc < docs/stellar-wimctl.1.scd` y `scdoc < docs/stellar-wim-ipc.7.scd` | Correcto |
| `meson test -C build-stellar --print-errorlogs` | **134/134 pruebas OK** |
| `test_ipc_protocol` | 6 pruebas de tramas, magic inválido, truncamiento, tokenización, escape JSON, límites y respuestas |
| `test_ipc_end_to_end` | Servidor headless real + CLI real: `version`, `capabilities`, `ping`, `windows list/active`, `desktops list/switch`, `outputs list`, `effects status`, comando inválido, id inexistente, suscripción a eventos, limpieza del socket al salir y configuración con IPC deshabilitado |
| Permisos del socket | La prueba verifica modo `0600`; comprobación manual adicional con ruta por omisión: `stellar-wim-wayland-0.sock` con modo `600` |
| Credenciales del par | La prueba automatizada rechazó un cliente de otro UID mediante `sudo -n -u nobody`: salida `connection closed` y registro `rejected IPC client with different uid` |
| Protección de socket ajeno | Con dos compositores headless apuntando al mismo socket, el segundo registró `IPC socket already in use: ...` y no borró ni sustituyó el socket del primero; el primero siguió respondiendo `ping` |
| Eventos | `EVENTS SUBSCRIBE` recibió `{"subscribed":true}` y una notificación real `EVENT {"type":"workspace",...}` al cambiar de escritorio |
| Deshabilitación | Con `<stellar-wim><ipc enabled="false"/></stellar-wim>` no se creó socket y el registro mostró `Stellar IPC disabled by configuration` |
| `stellar-wim --version` | `Stellar WiM 0.2.0 (based on Labwc 0.20.3) (+xwayland +nls +rsvg +libsfdo) wlroots-0.20.2` (versión de la Fase 2) |

## Fase 3 — pestañas y tiling opcionales

| Comprobación | Resultado |
| --- | --- |
| `meson setup build-stellar --reconfigure` | Correcto, proyecto `stellar-wim 0.3.0` |
| `ninja -C build-stellar` | Correcto; compila `src/tabs.c`, `src/tiling.c` y `src/tiling-layout.c` y enlaza `stellar-wim` y `stellar-wimctl` |
| `scdoc < docs/stellar-wim-config.5.scd`, `scdoc < docs/stellar-wim-ipc.7.scd`, `scdoc < docs/stellar-wimctl.1.scd`, `scdoc < docs/stellar-wim-actions.5.scd` | Correcto (las páginas se generan durante la compilación) |
| `meson test -C build-stellar --print-errorlogs` | **136/136 pruebas OK** (134 de Fase 2 + 2 nuevas) |
| `test_stellar_features` | 6 pruebas unitarias: layout de 1/2/3 ventanas, no solapamiento para 2–8 ventanas, ciclo de vida de grupos y limpieza de miembros al destruir un grupo |
| `test_stellar_features_end_to_end` | Compositor headless real con `tabs`/`tiling` activados: `version` con `features.tabs=true` y `features.tiling=true`, `capabilities` con ambos flags a 1, `tabs status`, `tabs list`, `tabs next` y `tabs group` con ids inexistentes, `tiling status`, `tiling reapply` y limpieza del socket al salir |
| Verificación en vivo con cliente xdg-shell real | Cliente Wayland mínimo compilado en `/tmp/opencode` (no incluido en el repositorio): dos ventanas mapeadas; `tabs group 1 0` devolvió `{"id":1,"ok":true}`, `tabs list` mostró `[{"id":1,"active":1,"members":[0,1]}]`, `tabs next` cambió `active` a 0 y `tabs detach 1` dejó un solo miembro |
| Tiling en vivo | Con dos ventanas mapeadas en la salida headless de 1280x720: `tiling status` → `{"enabled":true,"managed":2}` y geometría `x=0,w=768` / `x=768,w=512`, coherente con el layout maestro/stack 3/5 |
| Configuración por omisión | Sin `<tabs>` ni `<tiling>` en rc.xml, `version` y `capabilities` informan ambos flags en false/0; el modo flotante heredado sigue siendo el predeterminado |
| acciones nuevas | `StellarTabGroup`, `StellarTabNext`, `StellarTabPrevious`, `StellarTabDetach`, `StellarTilingToggle` y `StellarTilingReapply` registradas en `ACTION_TYPE_LIST` y despachadas en `run_action()` |

## Fase 4 — perfiles `minimal` / `enhanced`

| Comprobación | Resultado |
| --- | --- |
| `meson setup build-stellar --reconfigure` | Correcto, proyecto `stellar-wim 0.4.0` |
| `ninja -C build-stellar` | Correcto; compila `src/profile.c`, enlaza `stellar-wim` y `stellar-wimctl` y regenera las cuatro páginas de hombre |
| `scdoc < docs/stellar-wim-config.5.scd`, `scdoc < docs/stellar-wim-ipc.7.scd`, `scdoc < docs/stellar-wimctl.1.scd`, `scdoc < docs/stellar-wim-actions.5.scd` | Correcto |
| `meson test -C build-stellar --print-errorlogs` | **137/137 pruebas OK** (136 de Fase 3 + 1 nueva de integración; `test_stellar_features` ampliado de 6 a 8 pruebas unitarias) |
| `test_stellar_features` | 8 pruebas unitarias: layout de 1/2/3 ventanas, no solapamiento, ciclo de vida de grupos, limpieza de miembros, parseo/nombres de perfiles y aplicación/retorno de presets en caliente |
| `test_profiles_end_to_end` | Compositor headless real con `<profile>enhanced</profile>`: arranque con pestañas y tiling activos, `profile get`, `profile set minimal` y vuelta a `enhanced` comprobando `version`/`capabilities`/`tabs status`/`tiling status` en cada paso; rechazo de `bogus`, `custom` y argumentos sobrantes; evento real `{"type":"profile","event":"switch",...}`; `SIGHUP` reaplica el perfil configurado sin persistir el cambio en caliente; limpieza del socket al salir |
| Configuración por omisión | `"profile":"custom"` cuando no se especifica elemento, conservando el comportamiento de 0.3.x |
| Acciones nuevas | `StellarProfileMinimal` y `StellarProfileEnhanced` registradas en `ACTION_TYPE_LIST` y despachadas en `run_action()` |
| `stellar-wim --version` | `Stellar WiM 0.4.0 (based on Labwc 0.20.3) (+xwayland +nls +rsvg +libsfdo) wlroots-0.20.2` |

## Fase 5 — efectos visuales opcionales

| Comprobación | Resultado |
| --- | --- |
| `meson setup build-stellar --reconfigure` | Correcto, proyecto `stellar-wim 0.5.0` |
| `ninja -C build-stellar` | Correcto; compila `src/effects.c` y `src/effects-state.c`, enlaza `stellar-wim` y regenera las páginas de hombre |
| `scdoc < docs/stellar-wim-config.5.scd`, `scdoc < docs/stellar-wim-ipc.7.scd`, `scdoc < docs/stellar-wimctl.1.scd`, `scdoc < docs/stellar-wim-actions.5.scd` | Correcto |
| `meson test -C build-stellar --print-errorlogs` | **139/139 pruebas OK** (137 de Fase 4 + `test_effects` + `test_effects_end_to_end`) |
| `test_effects` | 5 pruebas unitarias: clamp, ease-out monótono y frontal, ciclo start/tick/complete, clamp de extremos y cancelación, rechazo de duración cero |
| `test_effects_end_to_end` | Compositor headless real: todos los efectos apagados por defecto, `animations/opacity/shadows/blur/enabled` correctos, `frame_commits` estable durante 0,5 s con efectos off y también con efectos on sin clientes, activación por SIGHUP, `opacity-level` clampado a 1.00 y 0.00, `VERSION.features.animations=true`, `CAPABILITIES.effects=1`, desactivación sin timer armado y limpieza del socket |
| Verificación en vivo con cliente xdg-shell real | Script `/tmp/opencode/live-fase5.sh` (fuera del repositorio): dos ventanas mapeadas con `animations=yes`; `animations_started=2`, `max active_animations=2`, `timer_armed=true` y `frame_commits` de 2 a 14 durante el fade; estado final `active_animations=0` y `timer_armed=false` |
| Sombras | No se reimplementan: se usa el `<theme><dropShadows>` heredado y `EFFECTS STATUS` informa su valor real |
| `stellar-wim --version` | `Stellar WiM 0.5.0 (based on Labwc 0.20.3) (+xwayland +nls +rsvg +libsfdo) wlroots-0.20.2` |

Durante la verificación en vivo se detectó y corrigió un fallo real: el timer
de wlroots es de un solo disparo y no se rearmaba, por lo que el fade quedaba
detenido con `timer_armed=true`. Tras rearmarlo en cada tick, el fade termina y
el timer se elimina. La corrección quedó cubierta por la repetición de la
prueba en vivo.

## Fase 6 — empaquetado, sesión y comparativa

| Comprobación | Resultado |
| --- | --- |
| `meson setup build-stellar --reconfigure` | Correcto, proyecto `stellar-wim 0.6.0` |
| `ninja -C build-stellar` | Correcto; genera también `stellar-wim-session.7` |
| `scdoc` de las cinco páginas Stellar | Correcto |
| `meson test -C build-stellar --print-errorlogs` | **140/140 pruebas OK** (139 de Fase 5 + `test_packaging`) |
| `test_packaging` | `meson install --destdir` correcto; presentes `stellar-wim`, `stellar-wimctl`, sesión, portales, ocho man pages, iconos y `rc.xml`; ausente cualquier ruta con nombre `labwc`; metadatos Debian `stellar-wim 0.6.0-1` en `debian/` |
| `meson dist -C build-stellar` | Correcto tras sustituir los `wrap-redirect` de `subprojects/` por definiciones `wrap-git` reales; el archivo resultante contiene `debian/` |
| `meson dist -C build-stellar --no-tests --include-subprojects` | Correcto; archivo de 2,4 MB con wlroots y subproyectos incluidos, por lo que el paquete Debian compila sin descargas |
| `sh packaging/build-deb.sh` | Genera `stellar-wim_0.6.0-1_amd64.deb` en la raíz del fork (2,4 MB); `dpkg-deb -I` confirma `stellar-wim 0.6.0-1`, y `dpkg-deb -c` confirma binarios, sesión, portales, man pages y ausencia de rutas `labwc` |
| Primera ejecución del script | Falló por dos causas reales: debhelper pasa `--wrap-mode=nodownload` (resuelto incluyendo subproyectos en el `meson dist`) y `mktemp` creaba un directorio 0700 que impedía el sondeo `SO_PEERCRED` como otro uid (resuelto con permisos 0755). Tras corregir también la búsqueda del `.deb` en `$work/src`, los pasos del script se ejecutaron manualmente y produjeron el paquete |
| Instalación con `apt-get install -y ./stellar-wim_0.6.0-1_amd64.deb` | Correcta (`rc=0`). El primer intento falló con código 100 por dos motivos: paquetes pendientes de configurar (`slurp`, `xdg-desktop-portal-wlr`, resuelto con `dpkg --configure -a`) y bibliotecas de subproyectos (`libv4l1.so.0.0.0`, etc.) que chocaban con `libv4l-0t64` (resuelto compilando subproyectos estáticos y eliminando `usr/lib` del paquete) |
| Comprobación posterior | `dpkg -l stellar-wim` → `ii 0.6.0-1`; `/usr/bin/stellar-wim --version` → `Stellar WiM 0.6.0 (based on Labwc 0.20.3) ...`; el `.deb` ya no contiene ningún fichero bajo `usr/lib` |
| `desktop-file-validate data/stellar-wim.desktop` | Informa `DesktopNames` como clave no estándar, igual que los ficheros de sesión de Plasma; las claves obligatorias (`Type`, `Name`, `Exec`, `TryExec`, `Icon`) se comprueban en `test_packaging` |
| Línea base Labwc 0.20.3 | Compilada desde el commit importado `d053d75` en `/tmp/stellar-baseline/build/labwc` con `-Dtest=disabled -Dman-pages=disabled -Dlabnag=disabled` |
| `sh t/benchmark.sh /tmp/stellar-baseline/build/labwc ./build-stellar/stellar-wim 5` | Mediana de arranque hasta el socket Wayland: 179 ms (Labwc) frente a 181 ms (Stellar). CPU en reposo 3 s: 0 ms en ambos. RSS mediana: 101560 KB frente a 101628 KB |
| `stellar-wim --version` | `Stellar WiM 0.6.0 (based on Labwc 0.20.3) (+xwayland +nls +rsvg +libsfdo) wlroots-0.20.2` |

La comparativa usa el mismo entorno headless, configuraciones equivalentes y
cinco repeticiones por compositor. Mide arranque, CPU en reposo y memoria
residente; **no** mide renderizado bajo carga ni latencia de entrada. La
diferencia de arranque está dentro del ruido de las muestras individuales
(178–181 ms frente a 180–185 ms).

## 0.7.0 — efectos activos por defecto

| Comprobación | Resultado |
| --- | --- |
| `meson setup build-stellar --reconfigure` | Correcto, proyecto `stellar-wim 0.7.0` |
| `ninja -C build-stellar` | Correcto |
| `meson test -C build-stellar --print-errorlogs` | **140/140 pruebas OK** con los nuevos defaults |
| `test_ipc_end_to_end` | `EFFECTS STATUS` devuelve `animations:true`, `opacity:true`, `shadows:true`, `enabled:true`, `blur:false` con una config que solo activa IPC |
| `test_effects_end_to_end` | Config por defecto: effects on, `opacity_level=0.9`, `VERSION.features.animations=true`, `CAPABILITIES.effects=1`, sin timers ni commits en reposo; opacidad 0.80/1.00/0.00 por SIGHUP; desactivación explícita devuelve los tres flags a false sin timer |
| `stellar-wim --version` | `Stellar WiM 0.7.0 (based on Labwc 0.20.3) (+xwayland +nls +rsvg +libsfdo) wlroots-0.20.2` |
| `sh packaging/build-deb.sh` | Genera `stellar-wim_0.7.0-1_amd64.deb` (1,9 MB). `meson dist` usa el árbol commiteado, por lo que los cambios debieron commitearse antes de regenerar el paquete |
| `dpkg-deb -I/-c` | `Package: stellar-wim`, `Version: 0.7.0-1`, sin ningún fichero bajo `usr/lib/` |
| `apt-get install -y ./stellar-wim_0.7.0-1_amd64.deb` | Correcto (`rc=0`); `dpkg -l stellar-wim` → `ii 0.7.0-1` |
| Prueba headless con el binario instalado y una config que solo activa IPC | `effects status` → `{"animations":true,"opacity":true,"shadows":true,"blur":false,"enabled":true,"opacity_level":0.90}` |

## 0.8.0 — cierre de las discrepancias de auditoría

| Comprobación | Resultado |
| --- | --- |
| Auditoría second-pass del código | Se revisaron todos los archivos modificados y el código que los invoca; se corrigieron los hallazgos accionables: fade-out pendiente al desactivar animaciones, opacidad recuperada si falla el timer, adición idempotente de miembros a un grupo, seguridad ante destrucción del segmento por un mousebinding, precedencia determinista de reglas de tiling, límites con aritmética `int64_t`, rechazo de valores no finitos y continuidad de opacidad al interrumpir un fade-in |
| Límite real de `TABS GROUP` | `STELLAR_IPC_MAX_TOKENS=40` y `STELLAR_IPC_MAX_GROUP_WINDOWS=32`; la prueba unitaria `test_ipc_protocol` verifica el rechazo de 41 tokens y `t/stellar-features.c` verifica grupos de tres ventanas y extensiones incrementales |
| `ninja -C build-stellar` | Correcto; regenera las cinco páginas de hombre de Stellar sin errores de `scdoc` |
| `meson test -C build-stellar --print-errorlogs` | **141/141 pruebas OK** con el código final y todas las páginas de hombre generadas |
| `test_effects_end_to_end` | Añade un caso con cliente GTK 3 real: minimizar inicia un fade-out (`active_animations>=1`, `timer_armed=true`), termina sin timer y deja la ventana marcada como minimizada y oculta |
| `test_stellar_features_end_to_end` | Tres ventanas GTK reales: agrupación de dos, extensión a tres con un miembro ya presente, `tabs next` con incremento de `animations_started`, comprobación de miembros mapeados y `tabs detach`; además verifica los cinco modos de tiling y el rechazo de un modo inválido |
| `test_profiles_end_to_end` | `minimal` desactiva y `enhanced` restaura `animations`, `opacity`, `shadows` y `enabled`, con `blur:false` en ambos |
| `test_ipc_end_to_end` | Vuelve a pasar tras ajustar la comprobación de credenciales: el cliente de otro UID debe fallar y el registro del servidor debe contener `rejected IPC client with different uid` |
| `t/benchmark.sh ../build-labwc/labwc ./build-stellar/stellar-wim 5` | Mediana de arranque 179 ms (Labwc 0.20.3) frente a 182 ms (Stellar 0.8.0); CPU en reposo 3 s: 0 ms en ambos; RSS mediana 103144 KB frente a 103244 KB |
| `stellar-wim --version` | `Stellar WiM 0.8.0 (based on Labwc 0.20.3) (+xwayland +nls +rsvg +libsfdo) wlroots-0.20.2` |
| Primer `meson dist` de 0.8.0 | Falló porque `.gitignore` ignoraba `build-aux/` y el script de aislamiento no entraba en el archivo; se ancló el patrón y se versionó el script |
| Primer `.deb` de 0.8.0 | Contenía `labnag`, `lab-sensible-terminal`, `labnag.1` y directorios vacíos heredados de subproyectos; se corrigió marcando esos destinos como no instalables y podando los directorios vacíos del `destdir` |
| `test_packaging` | Ampliado: comprueba ausencia de rutas `*labwc*`, `*labnag*` y `*lab-sensible-terminal*`, además de directorios vacíos, y la presencia de `VERIFICATION.md`; vuelve a pasar con **141/141** |
| `sh packaging/build-deb.sh` | Genera `stellar-wim_0.8.0-1_amd64.deb` (700 KB) desde el árbol commiteado; `meson dist` ejecuta de nuevo las 141 pruebas |
| `dpkg-deb -I/-c` | `Package: stellar-wim`, `Version: 0.8.0-1`, 66 archivos; no aparece ninguna ruta `labwc`, `labnag`, `lab-sensible-terminal`, `v4l*`, `rc_keymaps`, `usr/lib/`, `usr/include/` ni directorio vacío |
| `apt-get install -y ./stellar-wim_0.8.0-1_amd64.deb` | Correcto (`rc=0`); actualiza `stellar-wim` de `0.7.0-1` a `0.8.0-1` |
| Pruebas con los binarios instalados | `t/test-effects.sh /usr/bin/stellar-wim /usr/bin/stellar-wimctl` y `t/test-stellar-features.sh /usr/bin/stellar-wim /usr/bin/stellar-wimctl` pasan, incluido el fade-out de minimizar y el grupo de tres ventanas GTK |

La comparativa se repitió con el código final de 0.8.0, cinco repeticiones por
compositor, el mismo backend headless y configuraciones equivalentes. Mide
arranque hasta el socket Wayland, CPU en reposo y RSS; no mide renderizado bajo
carga ni latencia de entrada.

## Ronda 2 — cierre de la segunda auditoría

Esta ronda corrige los puntos que aún quedaban abiertos tras la auditoría
anterior. El número de versión no cambia: las correcciones se entregan como
parte de la misma serie 0.8.0 y el paquete final se regenera desde el commit
que las contiene.

| Comprobación | Resultado |
| --- | --- |
| Restauración de geometría flotante | `stellar_floating_saved`, `stellar_floating_geometry` y `stellar_manage_gen` guardan la geometría previa al tiling; al desactivar tiling (o al dejar de gestionar una ventana) se restaura, respetando maximize/fullscreen/tiled de Labwc. `view_adjust_size()` no sobrescribe esta geometría porque Stellar usa su propio snapshot |
| `t/test-advanced.sh` | Nueva prueba E2E con dos salidas headless: comprueba geometría antes/tiled/restaurada, `TILING STATUS` con `outputs[]`, `TILING TOGGLE`, cambio de pestaña activa al minimizar, close animado incluso con cambio de perfil a mitad del fade y excepción `stellarTiling="floating"` en modo hybrid |
| Pestañas | La pestaña activa minimizada cede la activación a un hermano visible; los grupos de un solo miembro se disuelven al cerrar/detach; la geometría compartida aplica `view_get_size_hints()`/`view_adjust_size()` de todos los miembros (mínimos obligatorios por encima de máximos incompatibles) |
| Tiling | `hybrid` es ahora partición real: un master gestionado y el resto flotante salvo regla `stellarTiling="managed"`; `min-width > max-width` (y versión de altura) se rechaza con log y se ignora el máximo; reapply tras move/resize interactivo y tras minimize/restore; el estado por salida informa modo efectivo |
| Efectos | `effects_on_view_map()` ignora vistas no mapeadas, minimizadas u ocultas por pestañas; `windows close` y la acción `Close` usan un fade-out que envía el close de protocolo solo al terminar, con guard de `view->mapped`; al desactivar animaciones durante el fade se entrega el close pendiente; `animations_started` incluye estos cierres |
| Acciones e IPC | Añadidos `StellarTilingSet`, `TILING TOGGLE` y `windows close`; documentados en las páginas man correspondientes |
| `ninja -C build-stellar` | Correcto; las cinco páginas man se regeneran sin errores de `scdoc` |
| `meson test -C build-stellar --print-errorlogs` | **142/142 pruebas OK** (141 heredadas + `test_advanced_end_to_end`) |
| ASan/LSan/UBSan | `meson setup build-asan -Db_sanitize=address,undefined -Db_lundef=false -Dtest=enabled`; compilación correcta y **15/15** pruebas del suite `stellar-wim` OK con `detect_leaks=1` y `halt_on_error=1` |
| Clientes reales | GTK 3, Qt 6 (`konsole`, `dolphin`) y XWayland (`xmessage`) aparecen como ventanas reales en `windows list` con la instancia 0.8.0 recién iniciada; XWayland informó `display :2` y `xmessage` se mapeó como `type:"xwayland"` |
| Captura real | `grim -o HEADLESS-1` produjo PNG 1280×720 en el backend headless con efectos activos; el histograma muestra múltiples colores y el log confirma que wlroots alternó direct scan-out al entrar/salir ventanas, coherente con la composición forzada por la opacidad |
| Paquete final | `meson dist` ejecutó 142/142 pruebas; `sh packaging/build-deb.sh` generó `stellar-wim_0.8.0-1_amd64.deb` con 67 archivos y sin rutas de Labwc/subproyectos; reinstalación correcta; `/usr/bin/stellar-wim --version` y `t/test-advanced.sh`, `t/test-effects.sh` y `t/test-stellar-features.sh` con los binarios instalados pasan |
| Documentación | `stellar-wim-config(5)`, `stellar-wim-actions(5)`, `stellar-wimctl(1)` y `stellar-wim-ipc(7)` describen la restauración de geometría, `stellarTiling`, modo hybrid real, límites invertidos, `TILING TOGGLE`, `StellarTilingSet`, persistencia de perfiles y close animado |

## 0.8.0 — API de configuración asíncrona

| Comprobación | Resultado |
| --- | --- |
| `ninja -C build-stellar` | Correcto; se regeneran las páginas man de `stellar-wim-config(5)` y `stellar-wim-ipc(7)` |
| `meson test -C build-stellar stellar-wim:test_ipc_end_to_end --print-errorlogs` | **1/1 OK**; incluye escrituras `CONFIG SET` en paralelo, desconexión del cliente antes de la respuesta y `CONFIG RELOAD` detrás de escrituras encoladas |
| `meson test -C build-stellar --print-errorlogs` | **142/142 pruebas OK** |
| `ninja -C build-asan` y `meson test -C build-asan --print-errorlogs` | Compilación correcta y **142/142 pruebas OK** con ASan/LSan/UBSan (`detect_leaks=1`, `halt_on_error=1`) |
| Concurrencia libxml2 | Se eliminaron las llamadas a `xmlCleanupParser()` durante recargas en `rcxml.c` y `menu.c`; el único hilo que parsea/escribe XML para la API es el trabajador serializado |
| Paquetes e instalación | No se generó ni instaló ningún `.deb` tras estos cambios; los binarios instalados siguen siendo anteriores a esta revisión |

## Limitaciones

* No se dispuso de una sesión Wayland real con GPU física; la ejecución se
  realizó con el backend headless y el renderer GLES2.
* XWayland sí se probó con `xmessage` en la ronda 2; la captura con clientes
  reales se hizo en headless, no en un escritorio físico.
* `windows activate`, `maximize`, `close` y `move-desktop` están implementados
  y son accesibles por IPC. `minimize` y `close` se ejercitan con ventanas GTK
  reales; `activate`, `maximize` y `move-desktop` solo se comprobaron con ids
  válidos en comprobaciones de estado, no con interacción visual completa.
* La detección de credenciales usa `sudo -u nobody` cuando `setpriv` no puede
  cambiar de UID; en sistemas sin ninguno de los dos mecanismos la prueba omite
  esa comprobación concreta y lo indica en su salida.
* La barra indicadora de pestañas se construye con `wlr_scene_rect` y se
  actualiza solo en cambios de grupo/pestaña/layout. Existe una captura real
  con `grim`, pero no una comparación pixel a pixel del aspecto de los
  rectángulos.
* La restauración de geometría flotante cubre el estado gestionado por Stellar.
  Si otro componente toma posesión de la ventana (maximize, fullscreen o tile
  heredado de Labwc) mientras Stellar la gestiona, ese estado ajeno conserva su
  geometría hasta que finaliza.
* Hay cinco modos de tiling, `master-ratio`, `columns`, límites de tamaño y
  reglas por ventana, pero no hay reorganización por arrastre ni múltiples zonas
  configurables. Una regla de tiling selectiva fija el modo de toda la salida;
  tiene prioridad la ventana activa y, en su defecto, la primera coincidencia.
* Los perfiles conmutan pestañas, tiling y efectos (animaciones, opacidad y
  sombras heredadas). No cambian renderer, adaptive sync, XWayland ni
  protocolos, y no se persisten: `SIGHUP` vuelve al perfil de `rc.xml`.
  `custom` no se puede seleccionar por IPC porque depende de los flags
  individuales del archivo de configuración.
* Hay fade-in al mapear o mostrar, fade-out al minimizar u ocultar una pestaña,
  y close animado para cierres solicitados por el compositor. El close es
  cooperativo: si el cliente ignora la petición, la ventana se desvanece de
  nuevo en lugar de destruirse. No hay animación de cambio de escritorio ni
  interpolación de geometría.
* La opacidad se aplica a los buffers de contenido del cliente, no a las
  decoraciones (que usan `wlr_scene_rect`). Fuerza composición en lugar de
  direct scanout; se capturó una imagen real y se comprobó el histograma de
  colores y el bloqueo de direct scan-out, pero no hay una prueba golden de
  píxel por píxel.
* No hay blur: wlroots 0.20 no expone ninguna operación de desenfoque en la API
  de escena. `EFFECTS STATUS` siempre informa `blur:false`.
* Se ejecutaron ASan/LSan/UBSan sobre las 15 pruebas del suite Stellar, sin
  errores. No se ha realizado una sesión prolongada con estrés de memoria, así
  que no se afirma estabilidad en sesiones largas.
