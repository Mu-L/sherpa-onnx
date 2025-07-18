// Copyright (c)  2023-2024  Xiaomi Corporation (authors: Fangjun Kuang)

const sherpa_onnx = require('sherpa-onnx');

function createRecognizer() {
  // Please download test files from
  // https://github.com/k2-fsa/sherpa-onnx/releases/tag/asr-models
  const config = {
    'modelConfig': {
      'moonshine': {
        'preprocessor': './sherpa-onnx-moonshine-tiny-en-int8/preprocess.onnx',
        'encoder': './sherpa-onnx-moonshine-tiny-en-int8/encode.int8.onnx',
        'uncachedDecoder':
            './sherpa-onnx-moonshine-tiny-en-int8/uncached_decode.int8.onnx',
        'cachedDecoder':
            './sherpa-onnx-moonshine-tiny-en-int8/cached_decode.int8.onnx',
      },
      'tokens': './sherpa-onnx-moonshine-tiny-en-int8/tokens.txt',
      'debug': 0,
    }
  };

  return sherpa_onnx.createOfflineRecognizer(config);
}

function createVad() {
  // please download silero_vad.onnx from
  // https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/silero_vad.onnx
  //
  // please download ten-vad.onnx from
  // https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/ten-vad.onnx
  //
  // You only need one vad
  //
  // To use ten-vad.onnx, please set sileroVad.model to ''
  // and set tenVad.model to 'ten-vad.onnx'
  //
  const config = {
    sileroVad: {
      model: './silero_vad.onnx',
      threshold: 0.5,
      minSpeechDuration: 0.25,
      minSilenceDuration: 0.5,
      maxSpeechDuration: 5,
      windowSize: 512,
    },
    tenVad: {
      // model: './ten-vad.onnx',
      model: '',
      threshold: 0.5,
      minSpeechDuration: 0.25,
      minSilenceDuration: 0.5,
      maxSpeechDuration: 5,
      windowSize: 256,
    },
    sampleRate: 16000,
    debug: true,
    numThreads: 1,
    bufferSizeInSeconds: 60,
  };


  return sherpa_onnx.createVad(config);
}

const recognizer = createRecognizer();
const vad = createVad();

// please download ./Obama.wav from
// https://github.com/k2-fsa/sherpa-onnx/releases/tag/asr-models
const waveFilename = './Obama.wav';
const wave = sherpa_onnx.readWave(waveFilename);

if (wave.sampleRate != recognizer.config.featConfig.sampleRate) {
  throw new Error(
      'Expected sample rate: ${recognizer.config.featConfig.sampleRate}. Given: ${wave.sampleRate}');
}

console.log('Started')
let start = Date.now();

let windowSize = vad.config.sileroVad.windowSize;
if (vad.config.tenVad.model != '') {
  windowSize = vad.config.tenVad.windowSize;
}

for (let i = 0; i < wave.samples.length; i += windowSize) {
  const thisWindow = wave.samples.subarray(i, i + windowSize);
  vad.acceptWaveform(thisWindow);

  while (!vad.isEmpty()) {
    const segment = vad.front();
    vad.pop();

    let start_time = segment.start / wave.sampleRate;
    let end_time = start_time + segment.samples.length / wave.sampleRate;

    start_time = start_time.toFixed(2);
    end_time = end_time.toFixed(2);

    const stream = recognizer.createStream();
    stream.acceptWaveform(wave.sampleRate, segment.samples);

    recognizer.decode(stream);
    const r = recognizer.getResult(stream);
    if (r.text.length > 0) {
      const text = r.text.toLowerCase().trim();
      console.log(`${start_time} -- ${end_time}: ${text}`);
    }

    stream.free();
  }
}

vad.flush();

while (!vad.isEmpty()) {
  const segment = vad.front();
  vad.pop();

  let start_time = segment.start / wave.sampleRate;
  let end_time = start_time + segment.samples.length / wave.sampleRate;

  start_time = start_time.toFixed(2);
  end_time = end_time.toFixed(2);

  const stream = recognizer.createStream();
  stream.acceptWaveform(wave.sampleRate, segment.samples);

  recognizer.decode(stream);
  const r = recognizer.getResult(stream);
  if (r.text.length > 0) {
    const text = r.text.toLowerCase().trim();
    console.log(`${start_time} -- ${end_time}: ${text}`);
  }
}

let stop = Date.now();
console.log('Done')

const elapsed_seconds = (stop - start) / 1000;
const duration = wave.samples.length / wave.sampleRate;
const real_time_factor = elapsed_seconds / duration;
console.log('Wave duration', duration.toFixed(3), 'seconds')
console.log('Elapsed', elapsed_seconds.toFixed(3), 'seconds')
console.log(
    `RTF = ${elapsed_seconds.toFixed(3)}/${duration.toFixed(3)} =`,
    real_time_factor.toFixed(3))

vad.free();
recognizer.free();
