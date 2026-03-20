<script>
  export let text = '';
  export let required = false;
</script>

<span class="field-label">
  {text}
  {#if required}
    <span class="required-mark" aria-label="Pflichtfeld" title="Pflichtfeld">*</span>
  {/if}
</span>

<style>
  .field-label {
    display: inline-flex;
    align-items: center;
    gap: 0.2rem;
    white-space: nowrap;
  }

  .required-mark {
    color: #ff5c5c;
    font-weight: 700;
    line-height: 1;
  }
</style>