const path = require('path');
const fs = require('fs');

process.env.NODE_PATH = path.join(__dirname, 'node_modules');
require('module').Module._initPaths();

const pptxgen = require('pptxgenjs');
const html2pptxCandidates = [
  path.join(process.env.USERPROFILE || '', '.codex', 'skills', 'skills', 'pptx', 'scripts', 'html2pptx.js'),
  path.join(process.env.USERPROFILE || '', '.codex', 'skills', 'pptx', 'scripts', 'html2pptx.js'),
  'C:\\Users\\Administrator\\.codex\\skills\\pptx\\scripts\\html2pptx.js'
];
const html2pptxPath = html2pptxCandidates.find((p) => p && fs.existsSync(p));
if (!html2pptxPath) {
  throw new Error(`Cannot find html2pptx.js. Tried: ${html2pptxCandidates.join(', ')}`);
}
const html2pptx = require(html2pptxPath);

async function build() {
  const pptx = new pptxgen();
  pptx.layout = 'LAYOUT_16x9';
  pptx.author = 'stm32Aquarium';
  pptx.title = '智能水族箱毕业设计答辩';

  const slides = [
    'slide01_cover.html',
    'slide02_outline.html',
    'slide03_background.html',
    'slide04_objectives.html',
    'slide05_architecture.html',
    'slide06_dataflow.html',
    'slide07_hardware_bom.html',
    'slide08_interfaces.html',
    'slide09_firmware_arch.html',
    'slide10_control_logic.html',
    'slide11_network_config.html',
    'slide12_cloud_iotda.html',
    'slide13_model_protocol.html',
    'slide14_app_arch.html',
    'slide15_ui_ux.html',
    'slide16_security.html',
    'slide17_testing.html',
    'slide18_metrics.html',
    'slide19_risk.html',
    'slide20_summary.html'
  ];

  for (const file of slides) {
    const htmlPath = path.join(__dirname, 'slides', file);
    const { slide, placeholders } = await html2pptx(htmlPath, pptx);

    if (file === 'slide18_metrics.html') {
      const chartArea = placeholders.find((p) => p.id === 'metrics-chart') || placeholders[0];
      if (chartArea) {
        slide.addChart(pptx.charts.BAR, [{
          name: '响应时延（秒）',
          labels: ['影子刷新', '命令回包', '重连恢复', '告警推送'],
          values: [0.8, 1.2, 2.5, 1.0]
        }], {
          ...chartArea,
          barDir: 'bar',
          showLegend: false,
          showTitle: true,
          title: '关键响应时延（示例）',
          showCatAxisTitle: true,
          catAxisTitle: '环节',
          showValAxisTitle: true,
          valAxisTitle: '时间（s）',
          valAxisMaxVal: 3,
          valAxisMinVal: 0,
          valAxisMajorUnit: 0.5,
          dataLabelPosition: 'outEnd',
          dataLabelColor: '1E293B',
          chartColors: ['2563EB']
        });
      }
    }
  }

  const outputPath = path.join(__dirname, 'aquarium-defense.pptx');
  await pptx.writeFile({ fileName: outputPath });
}

build().catch((err) => {
  console.error(err);
  process.exit(1);
});
