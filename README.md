# NEED2DREAMSCAPE

**Basado en [Petscop](https://www.youtube.com/@Petscop).** Proyecto independiente de horror en español.

Rade vuelve a casa en Navidad y encuentra a Dars junto a su computadora. Al encender la pantalla, la habitación cambia y empieza la búsqueda de Dars.

## Jugar en Windows

Descarga y extrae el ZIP. Abre `compile_and_playnow.cmd` para jugar. Si tienes GCC para Windows con SDL2 y zlib, el acceso compila el código C89 y lo ejecuta. En el resto de las instalaciones abre el ejecutable incluido. El mismo acceso funciona en una copia completa del repositorio. [Descargar NEED2DREAMSCAPE para Windows](https://github.com/h0nda1337/NEED2DREAMSCAPE/releases/latest)

- `game/jugar.cmd` abre el ejecutable incluido.
- `game/capitulo_2.cmd` abre el segundo capítulo si aún no hay una partida y retoma el progreso si ya existe.
- El progreso se guarda en `history-progress.n2d` en el Escritorio de Windows. Para recuperar una partida anterior, deja su `.save` en `game/` y elige **Cargar partida**. El primer arranque usa el `.save` más reciente si todavía no hay guardado en el Escritorio.

Usa WASD o las flechas para moverte, E para interactuar, clic o Enter para avanzar los diálogos e I para abrir el inventario del capítulo 2.

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
