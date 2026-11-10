# NEED2DREAMSCAPE

**Basado en [Petscop](https://www.youtube.com/@Petscop).** Proyecto independiente de horror en español.

Rade vuelve a casa en Navidad y encuentra a Dars junto a su computadora. Al encender la pantalla, la habitación cambia y empieza la búsqueda de Dars.

## Jugar en Windows

Descarga y extrae el ZIP. Abre `compile_and_playnow.cmd` para jugar. Si tienes GCC para Windows con SDL2 y zlib, el acceso compila el código C89 y lo ejecuta. En el resto de las instalaciones abre el ejecutable incluido. El mismo acceso funciona en una copia completa del repositorio. [Descargar NEED2DREAMSCAPE para Windows](https://github.com/h0nda1337/NEED2DREAMSCAPE/releases/latest)

- `game/jugar.cmd` abre el ejecutable incluido.
- `game/capitulo_3.cmd` permite probar el capítulo 3 con el guardado desactivado. Continuar desde una partida del capítulo 2 activa el guardado y conserva las decisiones anteriores.
- `game/capitulo_2.cmd` abre el segundo capítulo si aún no hay una partida y retoma el progreso si ya existe.
- **Cargar partida** muestra el autoguardado y los tres espacios manuales, con capítulo, mapa y fecha si está disponible.
- Pulsa **Esc** para pausar el juego, revisar el autoguardado y guardar o cargar partidas. Si el espacio ya está ocupado, pulsa **Guardar** otra vez para reemplazarlo.
- En el Escritorio de Windows quedan `history-progress.n2d` para el autoguardado y `history-progress-1.n2d`, `history-progress-2.n2d` y `history-progress-3.n2d` para los manuales. Si aún no hay autoguardado en el Escritorio, el juego puede recuperar el `.save` más reciente de `game/`.

Usa WASD o las flechas para moverte, E para interactuar, clic o Enter para avanzar los diálogos e I para abrir el inventario y el registro de investigación.

## Android

La compilación de prueba para Android incluye los mapas y recursos del juego, controles táctiles y guardado en el almacenamiento privado de la aplicación. Se adapta a la resolución del teléfono en horizontal. Las instrucciones para generar el APK están en [android/README.md](android/README.md).

En pantalla, mantén una flecha para caminar y desliza el dedo entre flechas para cambiar de dirección. PAUSA o el botón Atrás de Android abren el menú de guardado. OBJETOS abre el inventario y el registro de investigación.

En el capítulo 2, la señal que cambia de amarillo a azul marca el próximo punto del recorrido.

## Contenido de esta rama

- `game/` incluye el código C89, los recursos que necesita el juego y la compilación para Windows. Las dependencias y tipografías se acreditan en [`game/third_party/`](game/third_party/).
- `maps/` reúne los ocho mapas del capítulo. El exterior, la sala, el dormitorio, el cuarto de la computadora y el baño se rediseñaron con geometría propia. También están la cocina, el patio trasero y el pasillo equivocado. Cada mapa incluye geometría OBJ, materiales MTL y un JSON con coordenadas, colisiones, iluminación, puertas y puntos de evento. Los OBJ se pueden importar con su MTL en un editor 3D.
- `story/capitulo-1.json` contiene el diálogo del capítulo 1, desde la llegada de Rade hasta el cierre del pasillo.
- `art/figura.png` contiene los sprites dibujados para el juego de la Figura, Gwen, Hugh y Erik, en 40 × 60 píxeles con transparencia.
- `audio/` contiene efectos propios y grabaciones CC0. Las fuentes están en [Créditos de audio](CREDITOS_AUDIO.md).

## Recorrido del capítulo

Exterior → sala → dormitorio → cuarto de la computadora → dormitorio → sala → cocina → patio → cocina → dormitorio → cuarto de la computadora → pasillo equivocado.

Los ocho escenarios del primer capítulo se construyeron para NEED2DREAMSCAPE con geometría procedural y colores planos. La edición pública usa gráficos de control propios y Perfect DOS VGA en el título 3D del menú. [Créditos de las tipografías](game/third_party/TIPOGRAFIAS.md)


## Capítulo 2 — Los cuartos que no caben

Rade busca una salida del pasillo, vuelve a casa y se reúne con Dars. El capítulo termina en el cuarto de Dars. En `maps/` están los mapas propios y las variantes de la casa, con sus puertas y eventos. `story/capitulo-2.json` contiene el recorrido, el diálogo y sus variantes. Los efectos de audio sintetizados llevan el prefijo `cap2_`.


## Capítulo 3 — Las casas que nadie contó

Incluye las 120 escenas del guion, el final y las decisiones opcionales. Continúa desde el cierre del capítulo 2. Los nueve mapas nuevos son el cuarto de documentos, el paso de servicio, la ruta al archivo, la recepción y consulta municipal, la avenida industrial, el depósito de materiales, la biblioteca y su exterior. La casa tiene estados de mañana, tarde, noche y apagón.

La cinta métrica registra dos extremos seleccionados con E. Los muebles se pueden arrastrar manteniendo E al caminar, o alternando agarrar/soltar con E y con el botón táctil. Los documentos admiten páginas y ampliación mediante sus botones, las flechas y + / -. I u OBJETOS abren el registro de pruebas. Sus categorías —confirmado, hipótesis y descartado— se guardan con la partida. Durante el apagón, F o LUZ controlan la iluminación del teléfono.

Las elecciones con Dars, los documentos recogidos y las solicitudes al archivo se guardan por separado del capítulo anterior. El reloj continúa la cronología ficticia ya iniciada en el capítulo 2, con los cambios de día del guion. Las grabaciones de la llamada conservan las palabras de la escena original.

El guion y los eventos están en `story/capitulo-3.json`, las variantes de los mapas en `maps/` y los efectos originales nuevos usan el prefijo `c3_`. Los diálogos se muestran como texto animado con el sonido por personaje del juego. Las llamadas usan esa misma representación sonora.

`tests/run-chapter3.ps1` compila y ejecuta dos recorridos completos con movimiento y colisiones. Usa archivos temporales propios para probar el guardado.

## Capítulo 4 — Donde todavía quedan muros

Dars es la protagonista jugable de las 80 escenas. Su jornada pasa por su casa, el baño, el barrio, una visita familiar y su turno en la tienda. La segunda mitad transcurre en un Fortín de los Americanos explorable, con senderos, miradores y encuentros con la Figura. Continúa desde el cierre del capítulo 3. `game/capitulo_4.cmd` permite probar el capítulo con el guardado desactivado.

En la tienda, E toma o coloca mercancía, I lee la ficha, Q cambia de mano y R alterna el nivel bajo y medio. Los botones de la pantalla también permiten estas acciones. Cada SKU conserva cantidad, peso, estado y destino. El guardado registra dónde quedó cada artículo y permite retomar la clasificación. El carrito transporta las cajas pesadas y los estantes tienen límites de carga.

Los ocho mapas usan geometría original. La loma, los paredones de adobe y el cañón del sueño se inspiran en las postales de *Saltillo, imagen y memoria*, pp. 143–145. La distribución de los senderos y la ciudad del fondo son una interpretación artística. El epílogo utiliza ilustraciones propias basadas en las referencias. [Archivo Municipal de Saltillo](https://www.archivomunicipaldesaltillo.info/_files/ugd/e91b2b_8b37c46d77ba4966b58b1c3b2b16fe3c.pdf)

El guion está en `story/capitulo-4.json`. `tests/run-chapter4.ps1` prueba la continuación de una partida anterior y dos rutas de las 80 escenas, incluida la corrección de mercancía y el uso del carrito.
