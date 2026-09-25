#include "Metadata.h"

namespace fp {

const ModelMetadata* findModel(std::string_view name)
{
    for (const ModelMetadata& meta : modelTable()) {
        if (name == meta.name) {
            return &meta;
        }
    }
    return nullptr;
}

} // namespace fp
