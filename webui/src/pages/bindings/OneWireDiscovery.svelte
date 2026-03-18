<script>
  import { state } from '../../stores/state.js';
  import { t } from '../../stores/i18n.js';
  import Card from '../../components/Card.svelte';
  import NumberInput from '../../components/widgets/NumberInput.svelte';

  $: hasDs18b20 = !!$state['equipment.has_ds18b20_driver'];
</script>

{#if hasDs18b20}
  <Card title="DS18B20 / OneWire">
    <div class="settings-section">
      <NumberInput
        config={{ key: 'equipment.filter_coeff', description: $t['eq.filter'] || 'Цифровий фільтр', min: 0, max: 10, step: 1 }}
        value={$state['equipment.filter_coeff']}
      />
      <NumberInput
        config={{ key: 'equipment.ds18b20_offset', description: $t['eq.offset'] || 'Корекція °C', unit: '°C', min: -5, max: 5, step: 0.1 }}
        value={$state['equipment.ds18b20_offset']}
      />
    </div>
  </Card>
{/if}

<style>
  .settings-section {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }
</style>
