# Parche 309 · Fuera las funciones que nadie llama

**Hace falta `make`.**

El enlazador de x86_64 ya descartaba el código inalcanzable (`--gc-sections`, parche 199), así que el binario no cambia apenas.
Lo que se gana es **código que no hay que leer, mantener ni portar**: 52 funciones públicas y sus declaraciones, más las funciones
auxiliares que solo usaban ellas, en 38 archivos de C y sus cabeceras (517 líneas).

## Cómo se ha decidido qué es «sin uso» (criterio estricto)
La lista de `--print-gc-sections` **no basta**: el enlazador marca como «sin usar» la copia aparte de una función que el compilador
ha metido en línea donde se llama (por ejemplo, `tss_init` o `timer_sleep_ms`, que sí se usan). Así que solo se ha quitado una
función si cumple todo esto:
1. el enlazador la descarta en x86_64 (y no se usa en aarch64);
2. en todo el proyecto (`src/`, `tests/`, `tools/`, `Makefile`, `ejecutar.py`) su nombre aparece exactamente dos veces: su
   definición en un `.c` y su declaración en una cabecera;
3. las dos arquitecturas compilan con `-Werror` y pasan todas las pruebas sin ella.

Se han dejado dos que cumplían el criterio: `gui_stream_begin` y `gui_stream_push` (`gui_vortice.c`). Son la entrada del texto de la
respuesta a trozos en el orbe, una función que tiene sus pruebas (`test_orbe_259`, `test_orbe_266`) y que aún no está conectada.

## Lo que se quita
| Archivo | Funciones |
|---|---|
| `src/apps/browser.c` | `browser_set_element_text_by_id`, `browser_get_element_text_by_id`, `browser_js_eval`, settext_thunk, gettext_thunk y struct text_args (estaticas) |
| `src/apps/chat_core.c` | `cc_texto` |
| `src/apps/codeedit.c` | `ce_prompt_kind` |
| `src/apps/coder_core.c` | `cc_busy` |
| `src/apps/htmlproj.c` | `html_template_kinds` |
| `src/apps/image.c` | `image_render_alpha` |
| `src/apps/layout.c` | `layout_get_prompt` |
| `src/apps/notepad.c` | `notepad_handle_drag`, `notepad_handle_mouse_up` |
| `src/apps/pdfview.c` | `pdfview_pages` |
| `src/apps/web_js_big.c` | `js_big_from_i64`, `js_big_from_u64` |
| `src/apps/web_net.c` | `wn_set_gateway` |
| `src/apps/web_page.c` | `wp_post`, `wp_stop` |
| `src/arch/x86_64/ahci.c` | `ahci_ready`, `ahci_sector_count` |
| `src/arch/x86_64/apic.c` | `apic_routed` |
| `src/arch/x86_64/bootfb.c` | `bootinfo_loader`, `bootfb_console_active` |
| `src/arch/x86_64/iwl.c` | `iwl_get` |
| `src/arch/x86_64/nvme.c` | `nvme_ready` |
| `src/arch/x86_64/usermode.c` | `usermode_cpu_features` |
| `src/arch/x86_64/voz_sys.c` | `voz_tier_key` |
| `src/arch/x86_64/vplay_sys.c` | `vpx_active` |
| `src/arch/x86_64/web_r3.c` | `web_r3_frame_info` |
| `src/core/camera.c` | `camera_init`, `camera_present` |
| `src/core/mem.c` | `kheap_check_quick` |
| `src/core/pdfw.c` | `pdfw_pages` |
| `src/drivers/fb.c` | `fb_draw_circle`, `fb_set_mode_hd` |
| `src/drivers/mouse.c` | `mouse_right_held`, `mouse_get_speed` |
| `src/drivers/virtio_blk.c` | `sysdisk_device_name` |
| `src/fs/fs.c` | `vfs_tree`, `vfs_list`, `vfs_format_list` |
| `src/fs/srcfs.c` | `srcfs_ls` |
| `src/lang/lexer.c` | `token_type_name` |
| `src/net/dns.c` | `net_run_dns_test` |
| `src/net/http.c` | `http_request` |
| `src/net/httpd.c` | `httpd_init` |
| `src/net/ip.c` | `net_run_ping_test` |
| `src/ui/clipboard.c` | `clip_overlay_close` |
| `src/ui/console.c` | `console_uart_timeouts`, `console_uart_dead` |
| `src/ui/gui_abrir.c` | `gui_open_agents`, gui_open_agents__impl (estatica) |
| `src/ui/gui_font.c` | `font_draw_string_ansi`, `vicon_draw_tile`, font_draw_string_ansi_bitmap (estatica), vf_draw_string_ansi (estatica) |

Algunas eran restos claros de diseños anteriores: el puente de JavaScript de juguete del navegador (`browser_js_eval`...), que
usaba el `js_runtime.c` quitado en el 305; las pruebas de red de los primeros parches (`net_run_ping_test`, `net_run_dns_test`;
las órdenes `ping` y `dns` son otras); `httpd_init` (el servidor arranca por otro camino); `fb_set_mode_hd`;
`vfs_list`, `vfs_tree` y `vfs_format_list` (sustituidas por las órdenes del terminal), o `notepad_handle_drag` y
`notepad_handle_mouse_up` («compatibilidad con la interfaz anterior»).

## Comprobado
`make ci` (gcc 14), x86_64 y aarch64 con `WERROR=1` desde cero, `boot-smoke` en las dos y las pruebas de QEMU de la CI en
x86_64. `--revertir` deja el árbol idéntico.
