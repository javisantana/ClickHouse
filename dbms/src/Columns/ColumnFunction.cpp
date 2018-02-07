#include <Interpreters/ExpressionActions.h>
#include <Columns/ColumnFunction.h>
#include <Functions/IFunction.h>

namespace DB
{

namespace ErrorCodes
{
    extern const int LOGICAL_ERROR;
}

ColumnFunction::ColumnFunction(size_t size, FunctionBasePtr function, const ColumnsWithTypeAndName & columns_to_capture)
        : size_(size), function(function)
{
    captured_columns.reserve(function->getArgumentTypes().size());
    for (const auto & column : columns_to_capture)
        appendArgument(column);

}

MutableColumnPtr ColumnFunction::cloneResized(size_t size) const
{
    ColumnsWithTypeAndName capture = captured_columns;
    for (auto & column : capture)
        column.column = column.column->cloneResized(size);

    return ColumnFunction::create(size, function, capture);
}

MutableColumnPtr ColumnFunction::replicate(const Offsets & offsets) const
{
    if (size_ != offsets.size())
        throw Exception("Size of offsets doesn't match size of column.", ErrorCodes::SIZES_OF_COLUMNS_DOESNT_MATCH);

    ColumnsWithTypeAndName capture = captured_columns;
    for (auto & column : capture)
        column.column = column.column->replicate(offsets);

    return ColumnFunction::create(size_, function, capture);
}

MutableColumnPtr ColumnFunction::cut(size_t start, size_t length) const
{
    ColumnsWithTypeAndName capture = captured_columns;
    for (auto & column : capture)
        column.column = column.column->cut(start, length);

    return ColumnFunction::create(size_, function, capture);
}

MutableColumnPtr ColumnFunction::filter(const Filter & filt, ssize_t result_size_hint) const override
{
    ColumnsWithTypeAndName capture = captured_columns;
    for (auto & column : capture)
        column.column = column.column->filter(filt, result_size_hint);

    return ColumnFunction::create(size_, function, capture);
}

MutableColumnPtr ColumnFunction::permute(const Permutation & perm, size_t limit) const override
{
    ColumnsWithTypeAndName capture = captured_columns;
    for (auto & column : capture)
        column.column = column.column->permute(perm, limit);

    return ColumnFunction::create(size_, function, capture);
}


std::vector<MutableColumnPtr> ColumnFunction::scatter(IColumn::ColumnIndex num_columns,
                                                      const IColumn::Selector & selector) const
{
    std::vector<ColumnsWithTypeAndName> captures(num_columns, captured_columns);

    for (size_t capture = 0; capture < captured_columns.size(); ++capture)
    {
        auto parts = captured_columns[capture].column->scatter(num_columns, selector);
        for (IColumn::ColumnIndex part = 0; part < num_columns; ++part)
            captures[part][capture] = std::move(parts[part]);
    }

    std::vector<MutableColumnPtr> columns;
    columns.reserve(num_columns);
    for (auto & capture : captures)
    {
        size_t size__ = capture.front().column->size();
        columns.emplace_back(ColumnFunction::create(size__, function, std::move(capture)));
    }

    return columns;
}

void ColumnFunction::insertDefault()
{
    for (auto & column : captured_columns)
        column.column->insertDefault();
}
void ColumnFunction::popBack(size_t n)
{
    for (auto & column : captured_columns)
        column.column->popBack(n);
}

size_t ColumnFunction::byteSize() const
{
    size_t total_size = 0;
    for (auto & column : captured_columns)
        total_size += column.column->byteSize();

    return total_size;
}

size_t ColumnFunction::allocatedBytes() const
{
    size_t total_size = 0;
    for (auto & column : captured_columns)
        total_size += column.column->allocatedBytes();

    return total_size;
}

void ColumnFunction::appendArguments(const ColumnsWithTypeAndName & columns)
{
    for (const auto & column : columns)
        appendArgument(column);
}

void ColumnFunction::appendArgument(const ColumnWithTypeAndName & column)
{
    const auto & argumnet_types = function->getArgumentTypes();
    if (captured_columns.size() == argumnet_types.size())
        throw Exception("Cannot add argument " + std::to_string(argumnet_types.size() + 1) + "to ColumnFunction " +
                        "because all arguments are already captured.", ErrorCodes::LOGICAL_ERROR);

    auto index = captured_columns.size();
    if (!column.type->equals(*argumnet_types[index]))
        throw Exception("Cannot add argument " + std::to_string(argumnet_types.size() + 1) + "to ColumnFunction " +
                        "because it has incompatible type: got " + column.type->getName() +
                        ", but " + argumnet_types[index]->getName() + " is expected.", ErrorCodes::LOGICAL_ERROR);

    captured_columns.push_back(column);
}

ColumnWithTypeAndName ColumnFunction::reduce() const
{
    Block block(captured_columns);
    block.insert({nullptr, function->getReturnType(), ""});

    ColumnNumbers arguments(captured_columns.size());
    for (size_t i = 0; i < captured_columns.size(); ++i)
        arguments.push_back(i);

    function->execute(block, arguments, captured_columns.size());

    return block.getByPosition(captured_columns.size());
}

}
