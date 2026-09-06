<script>
  import Config from './Config.svelte';
  export let data;
  export let loadConfig;
  export let lang = 'de';
  export let onDirtyStateChange = () => {};
</script>
<Config bind:data {loadConfig} module="sensor" {lang} {onDirtyStateChange} />
