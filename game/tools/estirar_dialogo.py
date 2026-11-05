"""Genera versiones más largas de un aviso de diálogo sin repetirlo."""

import argparse
from pathlib import Path
import subprocess
import wave


LENGTHS_MS = (500, 650, 850, 1100, 1400, 1750, 2150, 2600, 3150, 3850)
RATE = 22050


def stretch(source, output, ffmpeg):
    with wave.open(str(source), 'rb') as original:
        duration = original.getnframes() / original.getframerate()
    if duration <= 0:
        raise ValueError('El WAV de origen está vacío.')

    output.mkdir(parents=True, exist_ok=True)
    for milliseconds in LENGTHS_MS:
        tempo = duration / (milliseconds / 1000)
        effect = (f'rubberband=tempo={tempo:.9f}:pitch=1:transients=smooth:'
                  'detector=soft:window=short')
        target = output / f'dialogo_estirado_{milliseconds:04d}.wav'
        subprocess.run([
            ffmpeg, '-hide_banner', '-loglevel', 'error', '-y', '-i', str(source),
            '-af', effect, '-ar', str(RATE), '-ac', '1', '-c:a', 'pcm_s16le',
            str(target),
        ], check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('origen', type=Path, help='WAV de diálogo que se va a estirar')
    parser.add_argument('salida', type=Path, help='Carpeta para los WAV generados')
    parser.add_argument('--ffmpeg', default='ffmpeg', help='FFmpeg con el filtro rubberband')
    args = parser.parse_args()
    stretch(args.origen, args.salida, args.ffmpeg)
