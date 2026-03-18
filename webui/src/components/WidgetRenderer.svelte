<script>
  import ValueWidget from './widgets/ValueWidget.svelte';
  import SliderWidget from './widgets/SliderWidget.svelte';
  import NumberInput from './widgets/NumberInput.svelte';
  import IndicatorWidget from './widgets/IndicatorWidget.svelte';
  import StatusText from './widgets/StatusText.svelte';
  import ToggleWidget from './widgets/ToggleWidget.svelte';
  import TextInput from './widgets/TextInput.svelte';
  import PasswordInput from './widgets/PasswordInput.svelte';
  import ButtonWidget from './widgets/ButtonWidget.svelte';
  import FirmwareUpload from './widgets/FirmwareUpload.svelte';
  import WifiSave from './widgets/WifiSave.svelte';
  import MqttSave from './widgets/MqttSave.svelte';
  import TimeSave from './widgets/TimeSave.svelte';
  import DatetimeInput from './widgets/DatetimeInput.svelte';
  import SelectWidget from './widgets/SelectWidget.svelte';
  import WifiScan from './widgets/WifiScan.svelte';
  import ApSave from './widgets/ApSave.svelte';
  import TimezoneSelect from './widgets/TimezoneSelect.svelte';
  import DefrostToggle from './widgets/DefrostToggle.svelte';
  import FileUploadWidget from './widgets/FileUploadWidget.svelte';
  import ChartWidget from './widgets/ChartWidget.svelte';
  import AuthSave from './widgets/AuthSave.svelte';
  import ActionsGrid from './widgets/ActionsGrid.svelte';
  import CloudSave from './widgets/CloudSave.svelte';
  import CertUpload from './widgets/CertUpload.svelte';

  export let widget;
  export let value;

  const widgetMap = {
    value: ValueWidget,
    slider: SliderWidget,
    number_input: NumberInput,
    indicator: IndicatorWidget,
    status_text: StatusText,
    toggle: ToggleWidget,
    text_input: TextInput,
    password_input: PasswordInput,
    button: ButtonWidget,
    firmware_upload: FirmwareUpload,
    wifi_save: WifiSave,
    mqtt_save: MqttSave,
    time_save: TimeSave,
    datetime_input: DatetimeInput,
    select: SelectWidget,
    wifi_scan: WifiScan,
    ap_save: ApSave,
    timezone_select: TimezoneSelect,
    defrost_toggle: DefrostToggle,
    file_upload: FileUploadWidget,
    chart: ChartWidget,
    auth_save: AuthSave,
    actions_grid: ActionsGrid,
    cloud_save: CloudSave,
    cert_upload: CertUpload
  };

  $: component = widgetMap[widget.widget] || ValueWidget;
</script>

<div class="widget-wrapper" data-widget-key={widget.key}>
  <svelte:component this={component} config={widget} {value} />
</div>

<style>
  .widget-wrapper {
    margin-bottom: var(--sp-3);
    min-height: var(--touch-min);
    display: flex;
    flex-direction: column;
    justify-content: center;
  }
  .widget-wrapper:last-child {
    margin-bottom: 0;
  }
</style>
