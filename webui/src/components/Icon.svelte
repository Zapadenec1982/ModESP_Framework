<script>
  import { icons } from '../lib/icons.js';
  export let name = 'home';
  export let size = 20;
</script>

<svg
  width={size}
  height={size}
  viewBox="0 0 24 24"
  fill="none"
  stroke="currentColor"
  stroke-width="2"
  stroke-linecap="round"
  stroke-linejoin="round"
  class="icon"
>
  {@html icons[name] || ''}
</svg>
