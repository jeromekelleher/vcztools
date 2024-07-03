#include "vcf_encoder.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

int
vcz_itoa(int32_t value, char *buf)
{
    int p = 0;
    int j, k;

    if (value < 0) {
        buf[p] = '-';
        p++;
        value = -value;
    }
    /*  special case small values */
    if (value < 10) {
        buf[p] = value + '0';
        p++;
    } else {
        k = 0;
        if (value < 100) {
            k = 1;
        } else if (value < 1000) {
            k = 2;
        } else if (value < 10000) {
            k = 3;
        } else if (value < 100000) {
            k = 4;
        } else if (value < 1000000) {
            k = 5;
        } else if (value < 10000000) {
            k = 6;
        } else if (value < 100000000) {
            k = 7;
        } else if (value < 1000000000) {
            k = 8;
        }

        // iterate backwards in buf
        p += k;
        buf[p] = (value % 10) + '0';
        for (j = 0; j < k; j++) {
            p--;
            value = value / 10;
            buf[p] = (value % 10) + '0';
        }
        p += k + 1;
    }
    buf[p] = '\0';
    return p;
}

static bool
int32_all_missing(const int32_t *restrict data, size_t n)
{
    size_t j;

    for (j = 0; j < n; j++) {
        if (data[j] != VCZ_INT_FILL && data[j] != VCZ_INT_MISSING) {
            return false;
        }
    }
    return true;
}

static bool
string_all_missing(const char *restrict data, size_t item_size, size_t n)
{
    size_t j;

    for (j = 0; j < n * item_size; j++) {
        if (data[j] != VCZ_STRING_FILL && data[j] != VCZ_STRING_MISSING) {
            return false;
        }
    }
    return true;
}

static int64_t
string_field_write_entry(
    const vcz_field_t *self, const void *data, char *dest, size_t buflen, int64_t offset)
{
    const char *source = (char *) data;
    size_t column, byte;
    const char sep = ',';
    int64_t source_offset = 0;

    for (column = 0; column < self->num_columns; column++) {
        if (column > 0 && source[source_offset] != '\0') {
            dest[offset] = sep;
            offset++;
        }
        for (byte = 0; byte < self->item_size; byte++) {
            if (source[source_offset] != '\0') {
                dest[offset] = source[source_offset];
                offset++;
            }
            source_offset++;
        }
    }
    dest[offset] = '\t';
    offset++;
    dest[offset] = '\0';
    return offset;
}

static int64_t
int32_field_write_entry(
    const vcz_field_t *self, const void *data, char *dest, size_t buflen, int64_t offset)
{
    const int32_t *source = (int32_t *) data;
    int32_t value;
    size_t column;
    /* int written; */
    /* char value_buffer[128]; */

    for (column = 0; column < self->num_columns; column++) {
        value = source[column];
        if (value != VCZ_INT_FILL) {
            if (column > 0) {
                dest[offset] = ',';
                offset++;
            }
            if (value == VCZ_INT_MISSING) {
                dest[offset] = '.';
                offset++;
            } else {
                offset += vcz_itoa(value, dest + offset);
                /* written = snprintf(value_buffer, sizeof(value_buffer), "%d", value);
                 */
                /* memcpy(dest + offset, value_buffer, written); */
                /* offset += written; */
            }
        }
    }
    dest[offset] = '\t';
    offset++;
    dest[offset] = '\0';
    return offset;
}

int64_t
vcz_field_write_entry(
    const vcz_field_t *self, const void *data, char *dest, size_t buflen, int64_t offset)
{

    if (self->type == VCZ_TYPE_INT) {
        if (self->item_size == 4) {
            return int32_field_write_entry(self, data, dest, buflen, offset);
        }
    } else if (self->type == VCZ_TYPE_STRING) {
        return string_field_write_entry(self, data, dest, buflen, offset);
    }
    return VCZ_ERR_FIELD_UNSUPPORTED_TYPE;
}

int64_t
vcz_field_write(
    const vcz_field_t *self, size_t variant, char *dest, size_t buflen, int64_t offset)
{

    size_t row_size = self->num_columns * self->item_size;
    const void *data = self->data + variant * row_size;

    return vcz_field_write_entry(self, data, dest, buflen, offset);
}

static bool
vcz_info_field_is_missing(const vcz_field_t *self, size_t variant)
{

    size_t row_size = self->num_columns * self->item_size;
    const void *data = self->data + variant * row_size;

    if (self->type == VCZ_TYPE_INT) {
        if (self->item_size == 4) {
            return int32_all_missing(data, self->num_columns);
        }
    } else if (self->type == VCZ_TYPE_STRING) {
        return string_all_missing(data, self->item_size, self->num_columns);
    }
    assert(false);
    return false;
}

static bool
vcz_format_field_is_missing(const vcz_field_t *self, size_t variant, size_t num_samples)
{
    size_t row_size = self->num_columns * self->item_size * num_samples;
    const void *data = self->data + variant * row_size;

    if (self->type == VCZ_TYPE_INT) {
        if (self->item_size == 4) {
            return int32_all_missing(data, self->num_columns * num_samples);
        }
    } else if (self->type == VCZ_TYPE_STRING) {
        return string_all_missing(data, self->item_size, self->num_columns * num_samples);
    }
    assert(false);
    return false;
}

void
vcz_field_print_state(const vcz_field_t *self, FILE *out)
{
    fprintf(out, "\t%s\ttype:%d\titem_size=%d\tnum_columns=%d\tdata=%p\n", self->name,
        self->type, (int) self->item_size, (int) self->num_columns, self->data);
}

int64_t
vcz_variant_encoder_write_sample_gt(const vcz_variant_encoder_t *self, size_t variant,
    size_t sample, char *dest, size_t buflen, int64_t offset)
{
    const size_t ploidy = self->gt.num_columns;
    size_t source_offset = variant * self->num_samples * ploidy + sample * ploidy;
    const int32_t *source = ((int32_t *) self->gt.data) + source_offset;
    const bool phased = self->gt_phased_data[variant * self->num_samples + sample];
    int32_t value;
    size_t ploid;
    char sep = phased ? '|' : '/';

    if (self->gt.item_size != 4) {
        offset = VCZ_ERR_FIELD_UNSUPPORTED_TYPE;
        goto out;
    }

    for (ploid = 0; ploid < ploidy; ploid++) {
        value = source[ploid];
        if (value != VCZ_INT_FILL) {
            if (ploid > 0) {
                dest[offset] = sep;
                offset++;
            }
            if (value == VCZ_INT_MISSING) {
                dest[offset] = '.';
                offset++;
            } else {
                offset += vcz_itoa(value, dest + offset);
                /* written = snprintf(value_buffer, sizeof(value_buffer), "%d", value);
                 */
                /* memcpy(dest + offset, value_buffer, written); */
                /* offset += written; */
            }
        }
    }
    dest[offset] = '\t';
    offset++;
    dest[offset] = '\0';
out:
    return offset;
}

/* int64_t */
/* vcz_variant_encoder_write_format_fields(const vcz_variant_encoder_t *self, */
/*     size_t variant, size_t sample, char *dest, size_t buflen, int64_t offset) */
/* { */
/*     vcz_field_t field; */
/*     size_t j, row_size; */
/*     const void *data; */

/*     if (self->gt.data != NULL) { */
/*         offset = vcz_variant_encoder_write_sample_gt( */
/*             self, variant, sample, dest, buflen, offset); */
/*         if (offset < 0) { */
/*             goto out; */
/*         } */
/*     } */

/*     for (j = 0; j < self->num_format_fields; j++) { */
/*         field = self->format_fields[j]; */
/*         dest[offset - 1] = ':'; */
/*         row_size = self->num_samples * field.num_columns * field.item_size; */
/*         data = field.data + variant * row_size */
/*                + sample * field.num_columns * field.item_size; */
/*         offset = vcz_field_write_entry(&field, data, dest, buflen, offset); */
/*         if (offset < 0) { */
/*             goto out; */
/*         } */
/*     } */
/* out: */
/*     return offset; */
/* } */

int64_t
vcz_variant_encoder_write_info_fields(const vcz_variant_encoder_t *self, size_t variant,
    char *dest, size_t buflen, int64_t offset)
{
    vcz_field_t field;
    size_t j;
    bool *missing = NULL;
    bool all_missing = true;
    bool first_field;

    if (self->num_info_fields > 0) {
        missing = malloc(self->num_info_fields * sizeof(*missing));
        if (missing == NULL) {
            offset = VCZ_ERR_NO_MEMORY;
            goto out;
        }
        for (j = 0; j < self->num_info_fields; j++) {
            missing[j] = vcz_info_field_is_missing(&self->info_fields[j], variant);
            if (!missing[j]) {
                all_missing = false;
            }
        }
    }

    if (all_missing) {
        dest[offset] = '.';
        offset++;
        dest[offset] = '\t';
        offset++;
    } else {
        first_field = true;
        for (j = 0; j < self->num_info_fields; j++) {
            if (!missing[j]) {
                if (!first_field) {
                    dest[offset - 1] = ';';
                }
                first_field = false;
                field = self->info_fields[j];
                memcpy(dest + offset, field.name, field.name_length);
                offset += field.name_length;
                dest[offset] = '=';
                offset++;
                offset = vcz_field_write(&field, variant, dest, buflen, offset);
                if (offset < 0) {
                    goto out;
                }
            }
        }
    }
out:
    if (missing != NULL) {
        free(missing);
    }
    return offset;
}


static int64_t
vcz_variant_encoder_write_format_fields(
    const vcz_variant_encoder_t *self, size_t variant, char *buf, size_t buflen, int64_t offset)
{
    size_t j, sample, row_size;
    vcz_field_t field;
    bool *missing = NULL;
    bool all_missing = true;
    bool has_gt = (self->gt.data != NULL);
    bool gt_missing = true;
    const size_t num_samples = self->num_samples;
    const void *data;

    if (has_gt) {
        gt_missing = vcz_format_field_is_missing(&self->gt, variant, num_samples);
    }

    if (self->num_format_fields > 0) {
        missing = malloc(self->num_format_fields * sizeof(*missing));
        if (missing == NULL) {
            offset = VCZ_ERR_NO_MEMORY;
            goto out;
        }
        for (j = 0; j < self->num_format_fields; j++) {
            missing[j] = vcz_format_field_is_missing(&self->format_fields[j], variant, num_samples);
            if (!missing[j]) {
                all_missing = false;
            }
        }
    }
    all_missing = all_missing && gt_missing;

    if (! all_missing) {

        if (!gt_missing) {
            strcpy(buf + offset, "GT:");
            offset += 3;
        }
        for (j = 0; j < self->num_format_fields; j++) {
            if (!missing[j]) {
                strcpy(buf + offset, self->format_fields[j].name);
                offset += self->format_fields[j].name_length;
                buf[offset] = ':';
                offset++;
            }
        }
        buf[offset - 1] = '\t';

        for (sample = 0; sample < num_samples; sample++) {
            if (!gt_missing) {
                offset = vcz_variant_encoder_write_sample_gt(
                    self, variant, sample, buf, buflen, offset);
                if (offset < 0) {
                    goto out;
                }
                buf[offset - 1] = ':';
            }
            for (j = 0; j < self->num_format_fields; j++) {
                if (!missing[j]) {
                    field = self->format_fields[j];
                    row_size = num_samples * field.num_columns * field.item_size;
                    data = field.data + variant * row_size
                           + sample * field.num_columns * field.item_size;
                    offset = vcz_field_write_entry(&field, data, buf, buflen, offset);
                    if (offset < 0) {
                        goto out;
                    }
                    buf[offset - 1] = ':';
                }
            }
            buf[offset - 1] = '\t';
        }
    }
out:
    if (missing != NULL) {
        free(missing);
    }
    return offset;
}

int64_t
vcz_variant_encoder_write_row(
    const vcz_variant_encoder_t *self, size_t variant, char *buf, size_t buflen)
{
    int64_t offset = 0;
    size_t j;

    for (j = 0; j < VCZ_NUM_FIXED_FIELDS; j++) {
        offset = vcz_field_write(&self->fixed_fields[j], variant, buf, buflen, offset);
        if (offset < 0) {
            goto out;
        }
    }
    offset = vcz_variant_encoder_write_info_fields(self, variant, buf, buflen, offset);
    if (offset < 0) {
        goto out;
    }
    offset = vcz_variant_encoder_write_format_fields(self, variant, buf, buflen, offset);
    if (offset < 0) {
        goto out;
    }
    offset--;
    buf[offset] = '\0';
out:
    return offset;
}

void
vcz_variant_encoder_print_state(const vcz_variant_encoder_t *self, FILE *out)
{
    size_t j;

    fprintf(out, "vcz_variant_encoder: %p\n", (void *) self);
    fprintf(out, "\tnum_samples: %d\n", (int) self->num_samples);
    fprintf(out, "\tnum_variants: %d\n", (int) self->num_variants);
    for (j = 0; j < VCZ_NUM_FIXED_FIELDS; j++) {
        vcz_field_print_state(&self->fixed_fields[j], out);
    }
    fprintf(out, "\tINFO:\n");
    for (j = 0; j < self->num_info_fields; j++) {
        vcz_field_print_state(&self->info_fields[j], out);
    }
    fprintf(out, "\tFORMAT:\n");
    if (self->gt.data != NULL) {
        vcz_field_print_state(&self->gt, out);
    }
    for (j = 0; j < self->num_format_fields; j++) {
        vcz_field_print_state(&self->format_fields[j], out);
    }
}

static int
vcz_variant_encoder_add_field(vcz_variant_encoder_t *self, vcz_field_t *field,
    const char *name, int type, size_t item_size, size_t num_columns, const void *data)
{
    int ret = 0;

    field->name_length = strlen(name);
    if (field->name_length >= VCZ_MAX_FIELD_NAME_LEN) {
        ret = VCZ_ERR_FIELD_NAME_TOO_LONG;
        goto out;
    }
    strcpy(field->name, name);
    field->type = type;
    field->item_size = item_size;
    field->num_columns = num_columns;
    field->data = data;
out:
    return ret;
}

int
vcz_variant_encoder_add_info_field(vcz_variant_encoder_t *self, const char *name,
    int type, size_t item_size, size_t num_columns, const void *data)
{
    int ret = 0;
    vcz_field_t *tmp, *field;

    if (self->num_info_fields == self->max_info_fields) {
        self->max_info_fields += self->field_array_size_increment;
        tmp = realloc(
            self->info_fields, self->max_info_fields * sizeof(*self->info_fields));
        if (tmp == NULL) {
            ret = VCZ_ERR_NO_MEMORY;
            goto out;
        }
        self->info_fields = tmp;
    }
    field = self->info_fields + self->num_info_fields;
    self->num_info_fields++;
    ret = vcz_variant_encoder_add_field(
        self, field, name, type, item_size, num_columns, data);
out:
    return ret;
}

int
vcz_variant_encoder_add_format_field(vcz_variant_encoder_t *self, const char *name,
    int type, size_t item_size, size_t num_columns, const void *data)
{
    int ret = 0;
    vcz_field_t *tmp, *field;

    if (self->num_format_fields == self->max_format_fields) {
        self->max_format_fields += self->field_array_size_increment;
        /* NOTE: assuming realloc(NULL) is safe and portable. check */
        tmp = realloc(
            self->format_fields, self->max_format_fields * sizeof(*self->format_fields));
        if (tmp == NULL) {
            ret = VCZ_ERR_NO_MEMORY;
            goto out;
        }
        self->format_fields = tmp;
    }
    field = self->format_fields + self->num_format_fields;
    self->num_format_fields++;

    ret = vcz_variant_encoder_add_field(
        self, field, name, type, item_size, num_columns, data);
out:
    return ret;
}

int
vcz_variant_encoder_add_gt_field(vcz_variant_encoder_t *self, const void *gt_data,
    size_t gt_item_size, size_t ploidy, const int8_t *gt_phased_data)
{
    strcpy(self->gt.name, "GT");
    self->gt.item_size = gt_item_size;
    self->gt.data = gt_data;
    self->gt.num_columns = ploidy;
    self->gt_phased_data = gt_phased_data;
    return 0;
}

// clang-format off
int
vcz_variant_encoder_init(vcz_variant_encoder_t *self,
    size_t num_samples, size_t num_variants,
    const char *chrom_data, size_t chrom_item_size,
    const int32_t *pos_data,
    const char *id_data, size_t id_item_size, size_t id_num_columns,
    const char *ref_data, size_t ref_item_size,
    const char *alt_data, size_t alt_item_size, size_t alt_num_columns,
    const int32_t *qual_data,
    const char *filter_data, size_t filter_item_size, size_t filter_num_columns)

{
    vcz_field_t fixed_fields[] = {
        { .name = "CHROM",
            .type = VCZ_TYPE_STRING,
            .item_size = chrom_item_size,
            .num_columns = 1,
            .data = chrom_data },
        { .name = "POS",
            .type = VCZ_TYPE_INT,
            .item_size = 4,
            .num_columns = 1,
            .data = pos_data },
        { .name = "ID",
            .type = VCZ_TYPE_STRING,
            .item_size = id_item_size,
            .num_columns = id_num_columns,
            .data = id_data },
        { .name = "REF",
            .type = VCZ_TYPE_STRING,
            .item_size = ref_item_size,
            .num_columns = 1,
            .data = ref_data },
        { .name = "ALT",
            .type = VCZ_TYPE_STRING,
            .item_size = alt_item_size,
            .num_columns = alt_num_columns,
            .data = alt_data },
        { .name = "QUAL",
            .type = VCZ_TYPE_INT,
            .item_size = 4,
            .num_columns = 1,
            .data = qual_data },
        { .name = "FILTER",
            .type = VCZ_TYPE_STRING,
            .item_size = filter_item_size,
            .num_columns = filter_num_columns,
            .data = filter_data } };

    // clang-format on

    memset(self, 0, sizeof(*self));
    self->num_samples = num_samples;
    self->num_variants = num_variants;
    self->field_array_size_increment = 64;
    memcpy(self->fixed_fields, fixed_fields, sizeof(fixed_fields));

    return 0;
}

void
vcz_variant_encoder_free(vcz_variant_encoder_t *self)
{
    if (self->info_fields != NULL) {
        free(self->info_fields);
        self->info_fields = NULL;
    }
    if (self->format_fields != NULL) {
        free(self->format_fields);
        self->format_fields = NULL;
    }
}
