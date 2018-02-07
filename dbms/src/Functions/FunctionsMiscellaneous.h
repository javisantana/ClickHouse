#pragma once

#include <Functions/IFunction.h>
#include <Interpreters/ExpressionActions.h>
#include <DataTypes/DataTypeFunction.h>
#include <IO/WriteBufferFromString.h>
#include <IO/Operators.h>
#include <Columns/ColumnFunction.h>

namespace DB
{

/** Creates an array, multiplying the column (the first argument) by the number of elements in the array (the second argument).
  * Used only as prerequisites for higher-order functions.
  */
class FunctionReplicate : public IFunction
{
public:
    static constexpr auto name = "replicate";
    static FunctionPtr create(const Context & context);

    String getName() const override
    {
        return name;
    }

    size_t getNumberOfArguments() const override
    {
        return 2;
    }

    bool useDefaultImplementationForNulls() const override { return false; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override;

    void executeImpl(Block & block, const ColumnNumbers & arguments, size_t result) override;
};

class FunctionExpression : public IFunctionBase, public IPreparedFunction,
                           public std::enable_shared_from_this<FunctionExpression>
{
public:
    FunctionExpression(ExpressionActionsPtr expression_actions, DataTypePtr return_type, std::string return_name)
            : expression_actions(std::move(expression_actions))
            , return_type(std::move(return_type)), return_name(std::move(return_name))
    {
        auto & arguments = expression_actions->getRequiredColumnsWithTypes();
        argument_types.reserve(arguments.size());

        for (const auto & argument : arguments)
            argument_types.push_back(argument.type);
    }

    String getName() const override { return "FunctionExpression"; }

    const DataTypes & getArgumentTypes() const override { return argument_types; }
    const DataTypePtr & getReturnType() const override { return return_type; }

    PreparedFunctionPtr prepare(const Block &) const override
    {
        return std::const_pointer_cast<FunctionExpression>(shared_from_this());
    }

    void execute(Block & block, const ColumnNumbers & arguments, size_t result) override
    {
        Block expr_block;
        for (auto argument : arguments)
            expr_block.insert(block.getByPosition(argument));

        expression_actions->execute(expr_block);

        block.getByPosition(result).column = expr_block.getByName(return_name).column;
    }

private:
    ExpressionActionsPtr expression_actions;
    DataTypes argument_types;
    DataTypePtr return_type;
    std::string return_name;
};

class FunctionCapture : public IFunctionBase, public IPreparedFunction, public FunctionBuilderImpl,
                        public std::enable_shared_from_this<FunctionCapture>
{
public:
    FunctionCapture(ExpressionActionsPtr expression_actions, const Names & captured,
                    DataTypePtr function_return_type, std::string expression_return_name)
            : expression_actions(std::move(expression_actions)), function_return_type(std::move(function_return_type)),
              expression_return_name(std::move(expression_return_name))
    {
        auto & all_arguments = expression_actions->getRequiredColumnsWithTypes();
        NameSet captured_args(captured.begin(), captured.end());

        for (const auto & argument : all_arguments)
        {
            if (captured_args.count(argument.name))
                captured_types.push_back(argument.type);
            else
                argument_types.push_back(argument.type);
        }

        return_type = std::make_shared<DataTypeFunction>(argument_types, function_return_type);

        name = "Capture[" + toString(captured_types) + "](" + toString(argument_types) +  ") -> "
               + function_return_type->getName();
    }

    String getName() const override { return name; }

    const DataTypes & getArgumentTypes() const override { return captured_types; }
    const DataTypePtr & getReturnType() const override { return return_type; }

    PreparedFunctionPtr prepare(const Block &) const override
    {
        return std::const_pointer_cast<FunctionCapture>(shared_from_this());
    }

    void execute(Block & block, const ColumnNumbers & arguments, size_t result) override
    {
        auto size = block.rows();
        ColumnsWithTypeAndName columns;
        columns.reserve(arguments.size());
        for (const auto & argument : arguments)
            columns.push_back(block.getByPosition(argument));

        auto function = std::make_shared<FunctionExpression>(expression_actions, function_return_type,
                                                             expression_return_name);
        block.getByPosition(result).column = ColumnFunction::create(size, std::move(function), columns);
    }

    size_t getNumberOfArguments() const override { return argument_types.size(); }

protected:
    DataTypePtr getReturnTypeImpl(const ColumnsWithTypeAndName &) const override { return return_type; }
    bool useDefaultImplementationForNulls() const override { return false; }
    FunctionBasePtr buildImpl(const ColumnsWithTypeAndName &, const DataTypePtr &) const override
    {
        return std::const_pointer_cast<FunctionCapture>(shared_from_this());
    }

private:
    std::string toString(const DataTypes & data_types) const
    {
        std::string result;
        {
            WriteBufferFromString buffer(result);
            bool first = true;
            for (const auto & type : data_types)
            {
                if (!first)
                    buffer << ", ";

                first = false;
                buffer << type->getName();
            }
        }

        return result;
    }

    ExpressionActionsPtr expression_actions;
    DataTypes captured_types;
    DataTypes argument_types;
    DataTypePtr function_return_type;
    DataTypePtr return_type;
    std::string expression_return_name;
    std::string name;
};

}
